"""One-job local generation bridge. DataCut owns this process; it owns its children.

The bridge never opens a terminal, browser, or persistent service. On Windows its
children join a kill-on-close Job Object, including if the editor crashes.
"""
from __future__ import annotations

import ctypes
import base64
import hashlib
import json
import os
from pathlib import Path
from functools import partial
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import traceback
from urllib.request import Request, urlopen
from urllib.error import HTTPError

_EMIT_LOCK = threading.Lock()
_PROGRESS_STATE = {"stage": "", "progress": 0.0, "last_event": time.monotonic()}

def emit(stage: str, progress: float | None = None, *, determinate: bool | None = None,
         **extra: object) -> None:
    value = {"stage": stage, "determinate": progress is not None if determinate is None else determinate, **extra}
    if progress is not None:
        value["progress"] = max(0.0, min(1.0, progress))
    with _EMIT_LOCK:
        _PROGRESS_STATE["stage"] = stage
        _PROGRESS_STATE["last_event"] = time.monotonic()
        if progress is not None:
            _PROGRESS_STATE["progress"] = max(_PROGRESS_STATE["progress"], value["progress"])
        print(json.dumps(value, ensure_ascii=False), flush=True)


def progress_heartbeat() -> None:
    """Keep long model-loading stages visibly moving when the provider is silent."""
    while True:
        time.sleep(2)
        with _EMIT_LOCK:
            stage = _PROGRESS_STATE["stage"]
            if stage in ("", "Failed", "Result ready"):
                continue
            if time.monotonic() - _PROGRESS_STATE["last_event"] < 4:
                continue
            progress = min(0.92, _PROGRESS_STATE["progress"] + 0.005)
            if progress <= _PROGRESS_STATE["progress"]:
                continue
            _PROGRESS_STATE["progress"] = progress
            print(json.dumps({"stage": stage + " · estimated", "progress": progress,
                              "determinate": True}), flush=True)


class ChildGroup:
    def __init__(self) -> None:
        self.processes: list[subprocess.Popen] = []
        self.handle = None
        if os.name == "nt":
            kernel = ctypes.windll.kernel32
            kernel.CreateJobObjectW.restype = ctypes.c_void_p
            kernel.SetInformationJobObject.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
            kernel.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            handle = kernel.CreateJobObjectW(None, None)
            if not handle:
                raise OSError("Could not create a Windows process job")
            # JOBOBJECT_EXTENDED_LIMIT_INFORMATION starts with basic limit info.
            # LimitFlags is DWORD at offset 16 on x64; size must cover the struct.
            info = ctypes.create_string_buffer(144)
            ctypes.c_uint32.from_buffer(info, 16).value = 0x2000  # KILL_ON_JOB_CLOSE
            if not kernel.SetInformationJobObject(handle, 9, info, len(info)):
                kernel.CloseHandle(handle)
                raise OSError("Could not set kill-on-close for worker children")
            self.handle = handle

    def start(self, args: list[str], cwd: Path, log: Path | None = None,
              env: dict[str, str] | None = None) -> subprocess.Popen:
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        output = log.open("wb") if log else subprocess.DEVNULL
        try:
            process = subprocess.Popen(args, cwd=cwd, stdin=subprocess.DEVNULL,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       creationflags=flags, env=env)
        finally:
            if log:
                output.close()
        if self.handle:
            if not ctypes.windll.kernel32.AssignProcessToJobObject(self.handle, ctypes.c_void_p(int(process._handle))):
                process.kill()
                raise OSError("Could not attach server to kill-on-close job")
        self.processes.append(process)
        return process

    def close(self) -> None:
        for process in self.processes:
            if process.poll() is None:
                process.kill()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        if self.handle:
            ctypes.windll.kernel32.CloseHandle(self.handle)
            self.handle = None


def request(url: str, payload: dict | None = None, timeout: float = 30) -> dict:
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    headers = {"Content-Type": "application/json"} if data else {}
    try:
        with urlopen(Request(url, data=data, headers=headers), timeout=timeout) as response:
            return json.load(response)
    except HTTPError as exc:
        detail = exc.read(1800).decode("utf-8", errors="replace")
        raise RuntimeError(f"Local runtime returned HTTP {exc.code}: {detail}") from exc


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_child(process: subprocess.Popen, log: Path, seconds: int, activity: str) -> None:
    try:
        code = process.wait(timeout=seconds)
    except subprocess.TimeoutExpired as exc:
        process.kill()
        raise RuntimeError(f"{activity} timed out. See {log}") from exc
    if code:
        detail = log.read_text(encoding="utf-8", errors="replace")[-1500:] if log.exists() else ""
        raise RuntimeError(f"{activity} failed: {detail or log}")


def wait_server(url: str, process: subprocess.Popen, seconds: int = 300) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Runtime server exited with code {process.returncode}")
        try:
            request(url, timeout=2)
            return
        except Exception:
            time.sleep(1)
    raise RuntimeError("Runtime server did not become ready")


def watch_comfy_progress(port: int, job_id: str) -> None:
    """Read ComfyUI's sampler events without adding a websocket dependency."""
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request_text = (f"GET /ws?clientId={job_id} HTTP/1.1\r\n"
                        f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n")
        sock.sendall(request_text.encode("ascii"))
        stream = sock.makefile("rb")
        if b"101" not in stream.readline():
            return
        while stream.readline().strip():
            pass
        sock.settimeout(None)
        last_progress = 0.15
        while True:
            header = stream.read(2)
            if len(header) != 2:
                return
            opcode = header[0] & 0x0F
            length = header[1] & 0x7F
            if length == 126:
                length = struct.unpack("!H", stream.read(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", stream.read(8))[0]
            mask = stream.read(4) if header[1] & 0x80 else None
            capture = opcode == 1 and length < 1024 * 1024
            chunks = []
            remaining = length
            while remaining:
                block = stream.read(min(65536, remaining))
                if not block:
                    return
                if capture:
                    chunks.append(block)
                remaining -= len(block)
            if opcode == 8:
                return
            if not capture:
                continue
            raw = b"".join(chunks)
            if mask:
                raw = bytes(value ^ mask[i % 4] for i, value in enumerate(raw))
            event = json.loads(raw)
            event_type = event.get("type")
            data = event.get("data") or {}
            if event_type == "progress" and data.get("max"):
                fraction = float(data.get("value", 0)) / float(data["max"])
                progress = max(last_progress, min(0.88, 0.15 + 0.73 * fraction))
                if progress - last_progress >= 0.005:
                    last_progress = progress
                    emit("Denoising", progress, determinate=True)
            elif event_type == "executing" and data.get("node") is None:
                emit("Finalizing generated media", 0.92)
    except (OSError, ValueError, json.JSONDecodeError):
        # Polling the history endpoint remains the authoritative completion path.
        return


def ensure_ltx_projection(runtime: Path) -> None:
    target = runtime / "models" / "text_encoders" / "ltx-2.3_text_projection_bf16.safetensors"
    expected_size = 2312149072
    expected_sha256 = "911d59bb4cb7708179c9a0045ea0fe41212ecfb77aed3a02702b7c0a8274911f"
    def matches(path: Path) -> bool:
        if not path.is_file() or path.stat().st_size != expected_size:
            return False
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(4 * 1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest() == expected_sha256

    if target.is_file():
        emit("Checking LTX text projection integrity", 0.03)
        if matches(target):
            return
        target.unlink()
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + ".part")
    if partial.exists() and partial.stat().st_size > expected_size:
        partial.unlink()
    source = "https://huggingface.co/Kijai/LTX2.3_comfy/resolve/main/text_encoders/ltx-2.3_text_projection_bf16.safetensors"
    offset = partial.stat().st_size if partial.exists() else 0
    if shutil.disk_usage(target.parent).free < max(0, expected_size - offset) + 500_000_000:
        raise RuntimeError("LTX text projection setup needs about 2.8 GB of free disk space")
    headers = {"Range": f"bytes={offset}-"} if offset else {}
    emit("Downloading LTX 2.3 text projection", 0.04)
    with urlopen(Request(source, headers=headers), timeout=60) as response:
        append = offset > 0 and response.status == 206
        if not append:
            offset = 0
        length = int(response.headers.get("Content-Length") or 0)
        total = offset + length
        received = offset
        last_report = time.monotonic()
        with partial.open("ab" if append else "wb") as file:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                file.write(chunk)
                received += len(chunk)
                if time.monotonic() - last_report > 0.5:
                    emit("Downloading LTX 2.3 text projection", 0.04 + 0.55 * received / max(total, received), determinate=True)
                    last_report = time.monotonic()
    if partial.stat().st_size != expected_size:
        raise RuntimeError("Downloaded LTX text projection has an unexpected size")
    if not matches(partial):
        raise RuntimeError("Downloaded LTX text projection failed its SHA-256 check")
    partial.replace(target)


def graph_image(options: dict, prefix: str) -> dict:
    width = max(256, min(1536, int(options.get("width", 768)))) // 16 * 16
    height = max(256, min(1536, int(options.get("height", 768)))) // 16 * 16
    seed = int(options.get("seed", -1))
    if seed < 0:
        seed = int.from_bytes(os.urandom(6), "big")
    prompt = str(options["prompt"])
    graph = {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": "flux-2-klein-4b.safetensors", "weight_dtype": "default"}},
        "2": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen_3_4b.safetensors", "type": "flux2", "device": "default"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "flux2-vae.safetensors"}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": prompt}},
        "5": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["4", 0]}},
        "6": {"class_type": "EmptyFlux2LatentImage", "inputs": {"width": width, "height": height, "batch_size": 1}},
        "7": {"class_type": "CFGGuider", "inputs": {"model": ["1", 0], "positive": ["4", 0], "negative": ["5", 0], "cfg": 1}},
        "8": {"class_type": "RandomNoise", "inputs": {"noise_seed": seed}},
        "9": {"class_type": "KSamplerSelect", "inputs": {"sampler_name": "euler"}},
        "10": {"class_type": "Flux2Scheduler", "inputs": {"steps": max(1, min(20, int(options.get("steps", 4)))), "width": width, "height": height}},
        "11": {"class_type": "SamplerCustomAdvanced", "inputs": {"noise": ["8", 0], "guider": ["7", 0], "sampler": ["9", 0], "sigmas": ["10", 0], "latent_image": ["6", 0]}},
        "12": {"class_type": "VAEDecode", "inputs": {"samples": ["11", 0], "vae": ["3", 0]}},
        "13": {"class_type": "SaveImage", "inputs": {"images": ["12", 0], "filename_prefix": prefix}},
    }
    reference = options.get("reference_name")
    if reference:
        graph["14"] = {"class_type": "LoadImage", "inputs": {"image": reference}}
        graph["15"] = {"class_type": "VAEEncode", "inputs": {"pixels": ["14", 0], "vae": ["3", 0]}}
        graph["16"] = {"class_type": "ReferenceLatent", "inputs": {"conditioning": ["4", 0], "latent": ["15", 0]}}
        graph["7"]["inputs"]["positive"] = ["16", 0]
    return graph


def graph_video(options: dict, prefix: str) -> dict:
    width = max(256, min(1024, int(options.get("width", 512)))) // 32 * 32
    height = max(256, min(1024, int(options.get("height", 512)))) // 32 * 32
    frames = max(9, min(241, int(options.get("frames", 49))))
    frames = 1 + (frames - 1) // 8 * 8
    seed = int(options.get("seed", -1))
    if seed < 0:
        seed = int.from_bytes(os.urandom(6), "big")
    graph = {
        "1": {"class_type": "UnetLoaderGGUF", "inputs": {"unet_name": "ltx-2.3-22b-distilled-UD-Q2_K.gguf"}},
        "2": {"class_type": "DualCLIPLoader", "inputs": {"clip_name1": "gemma-3-12b-it-heretic-v2_int4.safetensors", "clip_name2": "ltx-2.3_text_projection_bf16.safetensors", "type": "ltxv", "device": "default"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "ltx-2.3-22b-distilled_video_vae.safetensors"}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": str(options["prompt"])}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": str(options.get("negative", "blurry, distorted, low quality"))}},
        "6": {"class_type": "EmptyLTXVLatentVideo", "inputs": {"width": width, "height": height, "length": frames, "batch_size": 1}},
        "7": {"class_type": "CFGGuider", "inputs": {"model": ["16", 0], "positive": ["17", 0], "negative": ["17", 1], "cfg": float(options.get("guidance", 1.0))}},
        "8": {"class_type": "RandomNoise", "inputs": {"noise_seed": seed}},
        "9": {"class_type": "KSamplerSelect", "inputs": {"sampler_name": "euler"}},
        "10": {"class_type": "LTXVScheduler", "inputs": {"steps": max(1, min(40, int(options.get("steps", 8)))), "max_shift": 2.05, "base_shift": 0.95, "stretch": True, "terminal": 0.1, "latent": ["6", 0]}},
        "11": {"class_type": "SamplerCustomAdvanced", "inputs": {"noise": ["8", 0], "guider": ["7", 0], "sampler": ["9", 0], "sigmas": ["10", 0], "latent_image": ["6", 0]}},
        "12": {"class_type": "VAEDecodeTiled", "inputs": {"samples": ["11", 0], "vae": ["3", 0], "tile_size": 512, "overlap": 64, "temporal_size": 64, "temporal_overlap": 8}},
        "13": {"class_type": "VHS_VideoCombine", "inputs": {"images": ["12", 0], "frame_rate": int(options.get("fps", 24)), "loop_count": 0, "filename_prefix": prefix, "format": "video/h264-mp4", "pingpong": False, "save_output": True}},
        "16": {"class_type": "ModelSamplingLTXV", "inputs": {"model": ["1", 0], "max_shift": 2.05, "base_shift": 0.95, "latent": ["6", 0]}},
        "17": {"class_type": "LTXVConditioning", "inputs": {"positive": ["4", 0], "negative": ["5", 0], "frame_rate": float(options.get("fps", 24))}},
    }
    reference = options.get("reference_name")
    if reference:
        graph["14"] = {"class_type": "LoadImage", "inputs": {"image": reference}}
        graph["15"] = {"class_type": "LTXVImgToVideo", "inputs": {"positive": ["17", 0], "negative": ["17", 1], "vae": ["3", 0], "image": ["14", 0], "width": width, "height": height, "length": frames, "batch_size": 1, "strength": 1.0}}
        graph["7"]["inputs"]["positive"] = ["15", 0]
        graph["7"]["inputs"]["negative"] = ["15", 1]
        graph["10"]["inputs"]["latent"] = ["15", 2]
        graph["11"]["inputs"]["latent_image"] = ["15", 2]
        graph["16"]["inputs"]["latent"] = ["15", 2]
    return graph


def comfy_generate(payload: dict, group: ChildGroup, python: str) -> Path:
    runtime = Path(payload["runtime"])
    output = Path(payload["output_dir"])
    job_id = payload["job_id"]
    prefix = f"datacut-{job_id}"
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    log = output / f"{prefix}.log"
    temporary_input = None
    reference_path = payload.get("reference_image")
    if reference_path:
        source = Path(reference_path)
        if not source.is_file() or source.suffix.lower() not in (".png", ".jpg", ".jpeg", ".webp"):
            raise RuntimeError("Choose a PNG, JPEG or WebP reference image")
        temporary_input = runtime / "input" / f"datacut-{job_id}{source.suffix.lower()}"
        temporary_input.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, temporary_input)
        payload["reference_name"] = temporary_input.name
    emit("Starting local image/video runtime", 0.03)
    server = group.start([python, "-u", str(runtime / "main.py"), "--listen", "127.0.0.1",
                          "--port", str(port), "--disable-auto-launch", "--lowvram", "--output-directory", str(output)],
                         cwd=runtime, log=log)
    try:
        wait_server(base + "/system_stats", server)
        threading.Thread(target=watch_comfy_progress, args=(port, job_id), daemon=True).start()
        graph = graph_image(payload, prefix) if payload["kind"] == "image" else graph_video(payload, prefix)
        emit("Loading model and generating", 0.12)
        reply = request(base + "/prompt", {"prompt": graph, "client_id": job_id}, timeout=60)
        if "error" in reply:
            raise RuntimeError(str(reply["error"]) + " " + str(reply.get("node_errors", "")))
        prompt_id = reply.get("prompt_id")
        if not prompt_id:
            raise RuntimeError("ComfyUI did not accept the generation graph: " + str(reply))
        deadline = time.monotonic() + 3600
        emit("Generating in local runtime", 0.15)
        while time.monotonic() < deadline:
            if server.poll() is not None:
                raise RuntimeError(f"ComfyUI exited during generation. See {log}")
            history = request(base + "/history/" + prompt_id, timeout=15).get(prompt_id)
            if history:
                status = history.get("status", {})
                if status.get("status_str") == "error":
                    messages = status.get("messages") or []
                    detail = str(messages[-1])[-1000:] if messages else "See the runtime log"
                    raise RuntimeError(f"ComfyUI generation failed: {detail}. Log: {log}")
                for node in history.get("outputs", {}).values():
                    for entry in node.get("images", []) + node.get("gifs", []):
                        candidate = output / entry.get("subfolder", "") / entry["filename"]
                        if candidate.exists():
                            return candidate
                if status.get("completed"):
                    raise RuntimeError("ComfyUI completed but returned no media file")
            time.sleep(2)
        raise RuntimeError("Generation timed out after one hour")
    finally:
        if temporary_input:
            temporary_input.unlink(missing_ok=True)


def ace_generate(payload: dict, group: ChildGroup, python: str) -> Path:
    runtime = Path(payload["runtime"])
    output = Path(payload["output_dir"])
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    log = output / f"datacut-{payload['job_id']}.log"
    emit("Starting ACE-Step", 0.04)
    env = os.environ.copy()
    env.setdefault("HF_HUB_ENABLE_HF_TRANSFER", "0")
    env.setdefault("ACESTEP_CONFIG_PATH", "acestep-v15-turbo")
    env.setdefault("ACESTEP_CONFIG_PATH2", "acestep-v15-base")
    env.setdefault("ACESTEP_OFFLOAD_DIT_TO_CPU", "1")
    env.setdefault("ACESTEP_LM_OFFLOAD_TO_CPU", "1")
    api = group.start([python, "-u", "-m", "acestep.api_server", "--host", "127.0.0.1", "--port", str(port)],
                      cwd=runtime, log=log, env=env)
    wait_server(base + "/health", api, 600)
    emit("Loading ACE-Step Turbo model", 0.1)
    initialized = request(base + "/v1/init", {"model": "acestep-v15-turbo", "init_llm": False}, 600)
    if initialized.get("error"):
        raise RuntimeError("ACE-Step model initialization failed: " + str(initialized["error"]))
    options = payload
    data = {
        "task_type": "text2music", "prompt": options["prompt"],
        "lyrics": options.get("lyrics") or "[Instrumental]",
        "vocal_language": "unknown", "model": "acestep-v15-turbo",
        "audio_duration": max(10, min(300, float(options.get("duration", 30)))),
        "audio_format": "wav", "batch_size": 1,
        "inference_steps": max(1, min(100, int(options.get("steps", 8)))),
        "guidance_scale": 1.0, "shift": 3.0, "infer_method": "ode",
        "thinking": False, "use_tiled_decode": True,
        "use_random_seed": int(options.get("seed", -1)) < 0,
    }
    if int(options.get("seed", -1)) >= 0:
        data["seed"] = int(options["seed"])
    emit("Generating music", 0.15)
    reply = request(base + "/release_task", data, 120)
    task_id = (reply.get("data") or {}).get("task_id")
    if not task_id:
        raise RuntimeError("ACE-Step rejected the request: " + str(reply))
    deadline = time.monotonic() + 3600
    emit("Generating music", 0.2)
    while time.monotonic() < deadline:
        result = request(base + "/query_result", {"task_id_list": [task_id]}, 30)
        entries = result.get("data") or []
        if isinstance(entries, dict):
            item = entries.get(task_id) or entries.get("result") or entries
        else:
            item = next((entry for entry in entries if isinstance(entry, dict) and entry.get("task_id") == task_id), None)
        if isinstance(item, dict) and isinstance(item.get("result"), str):
            try:
                parsed = json.loads(item["result"])
                if isinstance(parsed, list) and parsed:
                    item = parsed[0]
                elif isinstance(parsed, dict):
                    item = parsed
            except ValueError:
                pass
        if isinstance(item, dict) and isinstance(item.get("progress"), (int, float)):
            fraction = float(item["progress"])
            if fraction > 1:
                fraction /= 100
            emit("Generating music", min(0.9, 0.2 + 0.7 * max(0.0, fraction)), determinate=True)
        if item and item.get("status") in (2, "failed"):
            raise RuntimeError(str(item.get("error") or item.get("message") or "ACE-Step failed"))
        if item and item.get("status") in (1, "succeeded"):
            # The API returns a local path; /v1/audio provides a safe copy.
            from urllib.parse import urlencode
            paths = item.get("result") or item.get("data") or item.get("audio_paths") or item.get("audios")
            if isinstance(paths, str):
                try:
                    paths = json.loads(paths)
                except ValueError:
                    paths = [{"path": paths}]
            if isinstance(paths, dict):
                paths = [paths]
            source = item.get("file") or item.get("audio_path") or item.get("path") or next((entry.get("file") or entry.get("path") or entry.get("audio_path")
                           for entry in (paths or []) if isinstance(entry, dict)), None)
            if not source:
                raise RuntimeError("ACE-Step returned no audio path: " + str(item))
            from urllib.parse import urlparse, parse_qs
            parsed_source = urlparse(source)
            if parsed_source.path.endswith("/v1/audio"):
                source = (parse_qs(parsed_source.query).get("path") or [source])[0]
            suffix = Path(source).suffix.lower()
            if suffix not in (".wav", ".flac", ".mp3", ".opus", ".aac", ".m4a"):
                suffix = ".wav"
            target = output / f"datacut-{payload['job_id']}{suffix}"
            with urlopen(base + "/v1/audio?" + urlencode({"path": source}), timeout=120) as response, target.open("wb") as file:
                shutil.copyfileobj(response, file)
            return target
        time.sleep(2)
    raise RuntimeError("ACE-Step generation timed out")


def sound_generate(payload: dict) -> Path:
    os.environ["HF_HOME"] = str(Path(payload["runtime"]) / "models" / "huggingface")
    from huggingface_hub import constants as hub_constants
    hub_constants.HF_TRANSFER_CONCURRENCY = 8
    import torch
    import torchaudio
    from tangoflux import TangoFluxInference

    output = Path(payload["output_dir"])
    target = output / f"datacut-{payload['job_id']}.wav"
    seed = int(payload.get("seed", -1))
    if seed < 0:
        seed = secrets.randbelow(2**31)
    steps = int(payload.get("steps", 50))
    if not 10 <= steps <= 100:
        raise ValueError("TangoFlux inference steps must be between 10 and 100")
    emit("Loading TangoFlux", 0.08)
    model = TangoFluxInference(name="declare-lab/TangoFlux")
    # TangoFluxInference.generate does not forward a seed and upstream
    # inference_flow resets the RNG to seed 0. Bind the requested seed to its
    # underlying method so -1 produces a fresh variation on every run.
    completed_steps = 0

    def step_finished() -> None:
        nonlocal completed_steps
        completed_steps += 1
        emit("Generating sound effect", 0.25 + 0.67 * completed_steps / steps,
             determinate=True)

    model.model.inference_flow = partial(model.model.inference_flow, seed=seed,
                                         callback_on_step_end=step_finished)
    emit("Generating sound effect", 0.25)
    audio = model.generate(str(payload["prompt"]),
                           steps=steps,
                           duration=max(1, min(30, int(payload.get("duration", 10)))))
    tensor = audio.detach().cpu() if isinstance(audio, torch.Tensor) else torch.tensor(audio)
    if tensor.ndim == 1:
        tensor = tensor.unsqueeze(0)
    if tensor.ndim == 3:
        tensor = tensor.squeeze(0)
    if tensor.ndim != 2:
        raise RuntimeError(f"TangoFlux returned unexpected audio shape {tuple(tensor.shape)}")
    if tensor.shape[0] not in (1, 2) and tensor.shape[1] in (1, 2):
        tensor = tensor.transpose(0, 1)
    torchaudio.save(str(target), tensor.float(), 44100)
    return target


def main() -> None:
    payload = json.loads(sys.stdin.readline())
    threading.Thread(target=progress_heartbeat, daemon=True).start()
    runtime = Path(payload["runtime"])
    output = Path(payload["output_dir"])
    output.mkdir(parents=True, exist_ok=True)
    os.environ["HF_HOME"] = str(runtime / "models" / "huggingface")
    os.environ["HF_HUB_ENABLE_HF_TRANSFER"] = "1" if payload["kind"] == "sound" else "0"
    for name in ("HF_HUB_CACHE", "HUGGINGFACE_HUB_CACHE", "HF_HUB_ENABLE_HF_XET", "TRANSFORMERS_CACHE"):
        os.environ.pop(name, None)
    kind = payload["kind"]
    group = ChildGroup()
    try:
        if payload["action"] == "setup":
            if not runtime.exists():
                raise RuntimeError("Runtime folder is missing. Choose an installed runtime folder in DataCut settings.")
            if kind in ("image", "video"):
                if kind == "video":
                    ensure_ltx_projection(runtime)
                port = free_port()
                base = f"http://127.0.0.1:{port}"
                log = output / f"datacut-setup-{kind}.log"
                emit("Starting ComfyUI for runtime check", 0.1)
                process = group.start([sys.executable, "-u", str(runtime / "main.py"), "--listen", "127.0.0.1", "--port", str(port), "--disable-auto-launch"], cwd=runtime, log=log)
                wait_server(base + "/system_stats", process)
                emit("Checking generation nodes", 0.7)
                nodes = request(base + "/object_info", timeout=60)
                needed = ("UNETLoader", "CLIPLoader", "EmptyFlux2LatentImage", "Flux2Scheduler") if kind == "image" else ("UnetLoaderGGUF", "EmptyLTXVLatentVideo", "LTXVScheduler", "VHS_VideoCombine")
                missing = [name for name in needed if name not in nodes]
                if missing:
                    raise RuntimeError("ComfyUI is missing required nodes: " + ", ".join(missing) + f". See {log}")
            elif kind == "music":
                port = free_port()
                base = f"http://127.0.0.1:{port}"
                log = output / "datacut-ace-setup.log"
                env = os.environ.copy()
                env.setdefault("ACESTEP_CONFIG_PATH", "acestep-v15-turbo")
                env.setdefault("ACESTEP_CONFIG_PATH2", "acestep-v15-base")
                env.setdefault("ACESTEP_OFFLOAD_DIT_TO_CPU", "1")
                env.setdefault("ACESTEP_LM_OFFLOAD_TO_CPU", "1")
                emit("Starting ACE-Step for runtime check", 0.15)
                process = group.start([sys.executable, "-u", "-m", "acestep.api_server", "--host", "127.0.0.1", "--port", str(port)], cwd=runtime, log=log, env=env)
                wait_server(base + "/health", process, 600)
                emit("Loading ACE-Step Turbo model", 0.65)
                initialized = request(base + "/v1/init", {"model": "acestep-v15-turbo", "init_llm": False}, 600)
                if initialized.get("error"):
                    raise RuntimeError("ACE-Step model initialization failed: " + str(initialized["error"]))
            elif kind == "sound":
                emit("Preparing TangoFlux weights", 0.58)
                log = output / "datacut-tangoflux-model.log"
                env = os.environ.copy()
                env["HF_HOME"] = str(runtime / "models" / "huggingface")
                code = ("import torch; from huggingface_hub import constants; "
                        "constants.HF_TRANSFER_CONCURRENCY=8; "
                        "from tangoflux import TangoFluxInference; "
                        "m=TangoFluxInference(name='declare-lab/TangoFlux'); "
                        "del m; torch.cuda.empty_cache()")
                process = group.start([sys.executable, "-c", code], cwd=runtime, log=log, env=env)
                hub = runtime / "models/huggingface/hub"
                tango = hub / "models--declare-lab--TangoFlux/blobs"
                encoder = hub / "models--google--flan-t5-large/blobs"
                weights = ((tango / "720ae39607a6aa95c2c8f6a49457268b0ce2a5cd4ef0b34d2eea756553cdb588", 3437051960),
                           (tango / "d73619a1d1e1dc48e606632931ffce440b4959ce2a4ed5a3522c3bb573b103be", 624490208),
                           (encoder / "a9dd06ce490f139af36e9eb77dd3758b4fd07a08a73d5a1abe5ff2591e2d388e", 3132668804))
                started = time.monotonic()
                previous_stage = ""
                previous_percent = -1
                while process.poll() is None:
                    if time.monotonic() - started > 1800:
                        process.kill()
                        raise RuntimeError(f"TangoFlux model setup timed out. See {log}")
                    received = sum(min(size, path.stat().st_size if path.exists()
                                       else path.with_name(path.name + ".incomplete").stat().st_size
                                       if path.with_name(path.name + ".incomplete").exists() else 0)
                                   for path, size in weights)
                    fraction = received / sum(size for _, size in weights)
                    encoder_partial = weights[2][0].with_name(weights[2][0].name + ".incomplete")
                    stage = ("Loading TangoFlux model" if fraction >= 1
                             else "Downloading FLAN-T5 encoder" if encoder_partial.exists()
                             else "Downloading TangoFlux weights")
                    percent = int(fraction * 100)
                    if stage != previous_stage or percent != previous_percent:
                        emit(stage, 0.58 + 0.38 * fraction, determinate=fraction < 1)
                        previous_stage, previous_percent = stage, percent
                    time.sleep(2)
                wait_child(process, log, 1, "TangoFlux model setup")
            emit("Runtime verified", 1)
            return
        if kind in ("image", "video"):
            result = comfy_generate(payload, group, sys.executable)
        elif kind == "music":
            result = ace_generate(payload, group, sys.executable)
        elif kind == "sound":
            result = sound_generate(payload)
        else:
            raise ValueError("Unknown generation kind")
        if not result.is_file() or result.stat().st_size == 0:
            raise RuntimeError("Runtime produced an empty file")
        emit("Result ready", 1, result=str(result))
    finally:
        group.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        emit("Failed", 0, error=str(exc))
        traceback.print_exc(file=sys.stderr)
        raise SystemExit(1)
