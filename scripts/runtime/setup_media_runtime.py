"""Provision one complete, private generation runtime for a clean Windows PC.

The editor starts this worker through setup_media_runtime.ps1. Nothing here probes
developer folders, system Python, or an existing ComfyUI installation.
"""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import traceback
from urllib.request import Request, urlopen
import zipfile


SOURCES = {
    "comfy": ("comfyanonymous/ComfyUI", "7f7fd918ed0815c5d5ef2ccfe44bfc8e72cfd217"),
    "gguf": ("city96/ComfyUI-GGUF", "6ea2651e7df66d7585f6ffee804b20e92fb38b8a"),
    "vhs": ("Kosinkadink/ComfyUI-VideoHelperSuite", "4d907bee61e92c2e65af3bd6383a4e4d356126d1"),
    "ace": ("ace-step/ACE-Step-1.5", "ca1e85fe9430179831e6bc6be790c332190a3866"),
    "tango": ("declare-lab/TangoFlux", "164420faa2b3f8a28bb013ccde7006422a616fc7"),
}


_PROGRESS_LOCK = threading.Lock()
_PROGRESS_STATE = {"stage": "", "progress": 0.0, "base": 0.0,
                   "last_event": time.monotonic()}


def emit(stage: str, progress: float | None = None, **extra: object) -> None:
    event: dict[str, object] = {"stage": stage, "determinate": progress is not None, **extra}
    if progress is not None:
        event.update(progress=progress, determinate=True)
    with _PROGRESS_LOCK:
        _PROGRESS_STATE["stage"] = stage
        _PROGRESS_STATE["last_event"] = time.monotonic()
        if progress is not None:
            _PROGRESS_STATE["progress"] = max(_PROGRESS_STATE["progress"], progress)
            _PROGRESS_STATE["base"] = progress
        print(json.dumps(event), flush=True)


def progress_heartbeat() -> None:
    while True:
        time.sleep(2)
        with _PROGRESS_LOCK:
            stage = _PROGRESS_STATE["stage"]
            if stage in ("", "Failed", "Runtime verified"):
                continue
            if time.monotonic() - _PROGRESS_STATE["last_event"] < 4:
                continue
            # A quiet child process gives no measurable progress. Keep the
            # estimate near its last real stage instead of racing to 94%.
            progress = min(0.94, _PROGRESS_STATE["base"] + 0.04,
                           _PROGRESS_STATE["progress"] + 0.003)
            if progress <= _PROGRESS_STATE["progress"]:
                continue
            _PROGRESS_STATE["progress"] = progress
            print(json.dumps({"stage": stage + " · estimated", "progress": progress,
                              "determinate": True}), flush=True)


def child(args: list[str], cwd: Path, log: Path, stage: str, progress: float) -> None:
    emit(stage, progress)
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with log.open("wb") as output:
        result = subprocess.run(args, cwd=cwd, stdin=subprocess.DEVNULL,
                                stdout=output, stderr=subprocess.STDOUT,
                                creationflags=flags, check=False)
    if result.returncode:
        detail = log.read_text(encoding="utf-8", errors="replace")[-2500:]
        loader = {
            0xC0000135: "A required Windows DLL is missing.",
            0xC0000139: "A Windows DLL has an incompatible entry point.",
            0xC000007B: "A Windows DLL has the wrong architecture.",
            0xC0000142: "A Windows DLL could not initialize.",
        }.get(result.returncode & 0xFFFFFFFF, "")
        raise RuntimeError(f"{stage} failed (exit {result.returncode}). "
                           f"{loader} {detail or 'See ' + str(log)}".strip())


def source(name: str, destination: Path, marker: str) -> None:
    repo, revision = SOURCES[name]
    stamp = destination / f".datacut-{name}-revision"
    if (destination / marker).is_file() and stamp.is_file() and stamp.read_text(encoding="utf-8") == revision:
        return
    emit(f"Downloading {repo} source")
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / f"datacut-{name}-source.zip"
    url = f"https://github.com/{repo}/archive/{revision}.zip"
    with urlopen(Request(url, headers={"User-Agent": "DataCut-runtime-setup"}), timeout=90) as response:
        with archive.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
    try:
        with zipfile.ZipFile(archive) as bundle:
            prefix = bundle.namelist()[0].split("/")[0] + "/"
            for member in bundle.infolist():
                if not member.filename.startswith(prefix) or member.is_dir():
                    continue
                relative = Path(member.filename[len(prefix):])
                if not relative.parts or ".." in relative.parts:
                    continue
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(member) as input_file, target.open("wb") as output:
                    shutil.copyfileobj(input_file, output)
    finally:
        archive.unlink(missing_ok=True)
    if not (destination / marker).is_file():
        raise RuntimeError(f"{repo} source did not contain {marker}.")
    stamp.write_text(revision, encoding="utf-8")


def model(repo: str, remote: str, target: Path, size: int, digest: str, stage: str,
          start: float, span: float) -> None:
    if target.is_file() and target.stat().st_size == size:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    if partial.exists() and partial.stat().st_size > size:
        partial.unlink()
    offset = partial.stat().st_size if partial.exists() else 0
    url = f"https://huggingface.co/{repo}/resolve/main/{remote}"
    headers = {"User-Agent": "DataCut-runtime-setup"}
    if offset:
        headers["Range"] = f"bytes={offset}-"
    emit(stage, start)
    with urlopen(Request(url, headers=headers), timeout=90) as response:
        append = offset and response.status == 206
        if not append:
            offset = 0
        downloaded = offset
        last = 0.0
        with partial.open("ab" if append else "wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
                downloaded += len(block)
                if time.monotonic() - last > 0.5:
                    emit(stage, start + span * min(downloaded / size, 1))
                    last = time.monotonic()
    if partial.stat().st_size != size:
        raise RuntimeError(f"{stage} was incomplete. Retry Setup to resume the download.")
    checksum = hashlib.sha256()
    with partial.open("rb") as downloaded_file:
        for block in iter(lambda: downloaded_file.read(4 * 1024 * 1024), b""):
            checksum.update(block)
    if checksum.hexdigest() != digest:
        partial.unlink()
        raise RuntimeError(f"{stage} failed its SHA-256 check. Retry Setup to download a clean copy.")
    partial.replace(target)


def pip(uv: Path, runtime: Path, output: Path, stage: str, progress: float,
        *packages: str) -> None:
    child([str(uv), "pip", "install", "--python", sys.executable, *packages],
          runtime, output / "datacut-media-install.log", stage, progress)


def native_preflight(uv: Path, runtime: Path, output: Path, progress: float) -> None:
    pip(uv, runtime, output, "Installing native dependency inspector", progress,
        "pefile==2024.8.26", "zstandard==0.24.0")
    worker = Path(__file__).with_name("native_runtime.py")
    child([sys.executable, "-u", str(worker), str(runtime)], runtime,
          output / "datacut-native-dependencies.log", "Checking private native libraries", progress)


def setup_comfy(kind: str, runtime: Path, output: Path, uv: Path) -> None:
    source("comfy", runtime, "main.py")
    pip(uv, runtime, output, "Installing matching CUDA PyTorch", 0.15,
        "torch==2.7.1", "torchvision==0.22.1", "torchaudio==2.7.1",
        "--index-url", "https://download.pytorch.org/whl/cu128")
    requirements = runtime / "requirements.txt"
    filtered = runtime / "bootstrap" / "comfy-requirements.txt"
    filtered.write_text("\n".join(line for line in requirements.read_text(encoding="utf-8").splitlines()
                                  if line.strip().lower() not in {"torch", "torchvision", "torchaudio"}),
                        encoding="utf-8")
    pip(uv, runtime, output, "Installing ComfyUI dependencies", 0.22,
        "-r", str(filtered))
    native_preflight(uv, runtime, output, 0.26)
    child([sys.executable, "-c",
           "import torch,torchvision; assert torch.cuda.is_available(), 'CUDA PyTorch unavailable'"],
          runtime, output / "datacut-comfy-cuda-check.log", "Checking image/video CUDA libraries", 0.29)
    if kind == "image":
        model("Comfy-Org/flux2-klein", "split_files/diffusion_models/flux-2-klein-4b.safetensors",
              runtime / "models/diffusion_models/flux-2-klein-4b.safetensors", 7751105712,
              "ec3d4e733a771f61c052fb4856c48b336c55eaf2c65487c2a1faeb9bbda7a343",
              "Downloading FLUX.2 Klein weights", 0.35, 0.23)
        model("Comfy-Org/flux2-klein", "split_files/text_encoders/qwen_3_4b.safetensors",
              runtime / "models/text_encoders/qwen_3_4b.safetensors", 8044982048,
              "6c671498573ac2f7a5501502ccce8d2b08ea6ca2f661c458e708f36b36edfc5a",
              "Downloading FLUX.2 text encoder", 0.58, 0.23)
        model("Comfy-Org/flux2-klein", "split_files/vae/flux2-vae.safetensors",
              runtime / "models/vae/flux2-vae.safetensors", 336211292,
              "868fe7b343cc8f3a19dbcfcafbc3d5f888802be3f89bd81b65b3621a066ce8f3",
              "Downloading FLUX.2 VAE", 0.81, 0.06)
    else:
        source("gguf", runtime / "custom_nodes/ComfyUI-GGUF", "__init__.py")
        source("vhs", runtime / "custom_nodes/ComfyUI-VideoHelperSuite", "__init__.py")
        pip(uv, runtime, output, "Installing video generation nodes", 0.28,
            "-r", str(runtime / "custom_nodes/ComfyUI-GGUF/requirements.txt"),
            "-r", str(runtime / "custom_nodes/ComfyUI-VideoHelperSuite/requirements.txt"))
        model("unsloth/LTX-2.3-GGUF", "distilled/ltx-2.3-22b-distilled-UD-Q2_K.gguf",
              runtime / "models/diffusion_models/ltx-2.3-22b-distilled-UD-Q2_K.gguf", 9306640928,
              "a93999b48942a16689877626e78ac26372337736e0a786406b2b2aea2e5a18d9",
              "Downloading LTX 2.3 weights", 0.36, 0.18)
        model("DreamFast/gemma-3-12b-it-heretic-v2", "comfyui/gemma-3-12b-it-heretic-v2_int4.safetensors",
              runtime / "models/text_encoders/gemma-3-12b-it-heretic-v2_int4.safetensors", 8250976554,
              "d3b8e6da888c1919170badc477d701e6ae5c87695e46e05d401af62a70efa3be",
              "Downloading LTX text encoder", 0.54, 0.17)
        model("Kijai/LTX2.3_comfy", "text_encoders/ltx-2.3_text_projection_bf16.safetensors",
              runtime / "models/text_encoders/ltx-2.3_text_projection_bf16.safetensors", 2312149072,
              "911d59bb4cb7708179c9a0045ea0fe41212ecfb77aed3a02702b7c0a8274911f",
              "Downloading LTX text projection", 0.71, 0.07)
        model("unsloth/LTX-2.3-GGUF", "vae/ltx-2.3-22b-distilled_video_vae.safetensors",
              runtime / "models/vae/ltx-2.3-22b-distilled_video_vae.safetensors", 1452256522,
              "e68d6d8f8a42942ac9b862cc315beb3bc30805a8876c7ad63ba5bf7a2b8e168a",
              "Downloading LTX video VAE", 0.78, 0.09)


def setup_music(runtime: Path, output: Path, uv: Path) -> None:
    source("ace", runtime, "pyproject.toml")
    child([str(uv), "sync", "--frozen", "--no-dev", "--python", sys.executable],
          runtime, output / "datacut-ace-install.log", "Installing ACE-Step and CUDA dependencies", 0.2)
    native_preflight(uv, runtime, output, 0.46)
    child([sys.executable, "-c",
           "import torch,torchaudio; assert torch.cuda.is_available(), 'CUDA PyTorch unavailable'"],
          runtime, output / "datacut-ace-cuda-check.log", "Checking ACE-Step CUDA libraries", 0.51)
    # The upstream downloader puts every required checkpoint in this runtime.
    child([sys.executable, "-m", "acestep.model_downloader"], runtime,
          output / "datacut-ace-models.log", "Downloading ACE-Step Turbo models", 0.62)


def setup_sound(runtime: Path, output: Path, uv: Path) -> None:
    source("tango", runtime, "setup.py")
    native_preflight(uv, runtime, output, 0.12)
    # TangoFlux pins the 2.4.0 ABI. Install that exact CUDA family together.
    cuda_check = [sys.executable, "-c",
                  "import torch,torchaudio,torchvision; "
                  "assert torch.__version__.startswith('2.4.0+cu121'); "
                  "assert torchaudio.__version__.startswith('2.4.0+cu121'); "
                  "assert torchvision.__version__.startswith('0.19.0+cu121'); "
                  "assert torch.cuda.is_available()"]
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with (output / "datacut-tango-cuda-check.log").open("wb") as check_log:
        compatible = subprocess.run(cuda_check, cwd=runtime, stdin=subprocess.DEVNULL,
                                    stdout=check_log, stderr=subprocess.STDOUT,
                                    creationflags=flags, check=False).returncode == 0
    if not compatible:
        pip(uv, runtime, output, "Installing matching CUDA PyTorch 2.4.0", 0.18,
            "--reinstall-package", "torch", "--reinstall-package", "torchvision",
            "--reinstall-package", "torchaudio",
            "torch==2.4.0", "torchvision==0.19.0", "torchaudio==2.4.0",
            "--index-url", "https://download.pytorch.org/whl/cu121")
        native_preflight(uv, runtime, output, 0.34)
    pip(uv, runtime, output, "Installing TangoFlux speech dependencies", 0.42,
        "--no-deps", "-e", str(runtime))
    pip(uv, runtime, output, "Installing TangoFlux inference packages", 0.58,
        "--reinstall-package", "huggingface-hub", "--reinstall-package", "numpy",
        "torchlibrosa==0.1.0", "transformers==4.44.0", "diffusers==0.30.0",
        "accelerate==0.34.2", "datasets==2.21.0", "librosa", "soundfile",
        "huggingface-hub==0.25.2", "hf_transfer==0.1.9", "numpy==1.26.4",
        "sentencepiece", "safetensors")
    child(cuda_check,
          runtime, output / "datacut-tango-cuda-check.log", "Checking TangoFlux CUDA libraries", 0.72)


def main() -> None:
    payload = json.loads(sys.stdin.readline())
    threading.Thread(target=progress_heartbeat, daemon=True).start()
    runtime = Path(payload["runtime"])
    output = Path(payload["output_dir"])
    output.mkdir(parents=True, exist_ok=True)
    os.environ["HF_HOME"] = str(runtime / "models" / "huggingface")
    os.environ["HF_HUB_ENABLE_HF_TRANSFER"] = "0"
    os.environ["HF_HUB_DISABLE_IMPLICIT_TOKEN"] = "1"
    for name in ("HF_HUB_CACHE", "HUGGINGFACE_HUB_CACHE", "HF_HUB_ENABLE_HF_XET", "TRANSFORMERS_CACHE"):
        os.environ.pop(name, None)
    uv = Path(sys.argv[1])
    kind = payload["kind"]
    if kind in ("image", "video"):
        setup_comfy(kind, runtime, output, uv)
    elif kind == "music":
        setup_music(runtime, output, uv)
    elif kind == "sound":
        setup_sound(runtime, output, uv)
    else:
        raise RuntimeError("Unknown generation runtime")
    emit("Checking installed runtime", 0.9)
    worker = Path(__file__).with_name("media_generate.py")
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    verify_log = output / f"datacut-{kind}-verify.log"
    with verify_log.open("wb") as errors:
        process = subprocess.Popen([sys.executable, "-u", str(worker)], cwd=runtime,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=errors, text=True, encoding="utf-8", errors="replace",
                                   creationflags=flags)
        assert process.stdin is not None and process.stdout is not None
        process.stdin.write(json.dumps(payload) + "\n")
        process.stdin.close()
        for line in process.stdout:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "error" in event:
                emit("Failed", 0, error=event["error"])
            elif "stage" in event:
                progress = (0.9 + 0.09 * float(event.get("progress", 0)))
                emit(str(event["stage"]), progress if event.get("determinate", False) else None)
        code = process.wait()
    if code:
        detail = verify_log.read_text(encoding="utf-8", errors="replace")[-2500:]
        raise RuntimeError(detail or f"Installed runtime could not be verified. See {verify_log}")
    marker = runtime / ".datacut-verified.json"
    temporary = marker.with_suffix(".tmp")
    temporary.write_text(json.dumps({"schemaVersion": 1, "kind": kind}), encoding="utf-8")
    temporary.replace(marker)
    emit("Runtime verified", 1)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        emit("Failed", 0, error=str(error))
        traceback.print_exc(file=sys.stderr)
        raise SystemExit(1)
