"""Short-lived SeedVC speech conversion worker; JSON requests and progress use stdio."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback


def event(stage: str, progress: float, **extra: object) -> None:
    print(json.dumps({"stage": stage, "progress": progress, **extra}), flush=True)


# The upstream wrapper truncates target audio to 25 seconds before encoding it.
MAX_REFERENCE_SECONDS = 25


def run(command: list[str], cwd: Path | None = None) -> None:
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    subprocess.run(command, cwd=cwd, check=True, stdin=subprocess.DEVNULL,
                   creationflags=flags)


def model_files_present(runtime: Path) -> bool:
    cache = runtime / "checkpoints" / "models--Plachta--Seed-VC" / "snapshots"
    return any((snapshot / "DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth").stat().st_size > 100_000_000
               for snapshot in cache.iterdir() if snapshot.is_dir()
               and (snapshot / "DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth").is_file()) if cache.is_dir() else False


def setup(request: dict) -> None:
    runtime = Path(request["runtime"]).resolve()
    python = Path(request["python"]).resolve()
    if not (runtime / "seed_vc_wrapper.py").is_file() or not python.is_file():
        raise FileNotFoundError("SeedVC source or isolated Python is missing; run setup first")
    if not model_files_present(runtime):
        raise FileNotFoundError("SeedVC speech checkpoint is missing; run setup first")
    os.environ["HF_HUB_ENABLE_HF_TRANSFER"] = "0"
    os.environ["HF_HUB_CACHE"] = str(runtime / "checkpoints" / "hf_cache")
    event("Checking SeedVC dependencies", 0.5)
    run([str(python), "-c", "import torch, torchaudio, librosa, soundfile, transformers; from seed_vc_wrapper import SeedVCWrapper; assert torch.cuda.is_available()"], cwd=runtime)
    event("SeedVC speech runtime ready", 1.0)


def generate(request: dict) -> None:
    runtime = Path(request["runtime"]).resolve()
    os.environ["HF_HUB_ENABLE_HF_TRANSFER"] = "0"
    os.environ["HF_HUB_CACHE"] = str(runtime / "checkpoints" / "hf_cache")
    import numpy as np
    import soundfile as sf
    import torch

    source = Path(request["source"]).resolve()
    target = Path(request["target"]).resolve()
    output = Path(request["output"]).resolve()
    ffmpeg = request.get("ffmpeg")
    if not ffmpeg or not Path(ffmpeg).is_file():
        ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg or not source.is_file() or not target.is_file():
        raise FileNotFoundError("FFmpeg, source audio, or target recording is missing")
    if not torch.cuda.is_available():
        raise RuntimeError("SeedVC speech conversion needs an NVIDIA CUDA GPU")
    if not model_files_present(runtime):
        raise FileNotFoundError("SeedVC speech checkpoint is missing; run setup first")
    start = float(request["start"])
    duration = float(request["duration"])
    reference_start = float(request.get("reference_start", 0))
    reference_duration = float(request.get("reference_duration", 10))
    steps = int(request.get("cfm_steps", 50))
    cfg = float(request.get("speaker_cfg", 1.2))
    if start < 0 or duration <= 0 or reference_start < 0 or not 1 <= reference_duration <= MAX_REFERENCE_SECONDS:
        raise ValueError("Choose a valid source and a 1–25 second target excerpt")
    if not 1 <= steps <= 100 or not 0 <= cfg <= 3:
        raise ValueError("SeedVC needs 1–100 diffusion steps and CFG between 0 and 3")

    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.with_suffix(".work")
    work.mkdir(exist_ok=False)
    try:
        source_wav = work / "source.wav"
        target_wav = work / "target.wav"
        event("Preparing source and target speech", 0.04)
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
             "-ss", f"{start:.9f}", "-i", str(source), "-t", f"{duration:.9f}",
             "-vn", "-ac", "1", "-ar", "22050", "-c:a", "pcm_s16le", str(source_wav)])
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
             "-ss", f"{reference_start:.9f}", "-i", str(target),
             "-t", f"{reference_duration:.9f}", "-vn", "-ac", "1", "-ar", "22050",
             "-c:a", "pcm_s16le", str(target_wav)])
        if sf.info(target_wav).duration + 0.03 < reference_duration or sf.info(source_wav).duration < 0.1:
            raise ValueError("The target excerpt extends past the recording or the source has too little audio")
        os.chdir(runtime)  # Upstream resolves its checkpoint cache relative to cwd.
        sys.path.insert(0, str(runtime))
        from seed_vc_wrapper import SeedVCWrapper

        event("Loading SeedVC speech model", -1)
        wrapper = SeedVCWrapper()
        event("Converting speech", -1)
        generator = wrapper.convert_voice(
            source=str(source_wav), target=str(target_wav),
            diffusion_steps=steps, length_adjust=1.0,
            inference_cfg_rate=cfg, f0_condition=False,
            auto_f0_adjust=False, pitch_shift=0, stream_output=False)
        try:
            while True:
                next(generator)
        except StopIteration as stopped:
            converted = stopped.value
        if converted is None or len(converted) == 0:
            raise RuntimeError("SeedVC returned no speech")
        converted = np.asarray(converted, dtype=np.float32)
        required = round(sf.info(source_wav).duration * wrapper.sr)
        # Keep the output aligned with its original timeline clip.
        if abs(len(converted) - required) > wrapper.sr // 50:
            import librosa
            converted = librosa.effects.time_stretch(converted, rate=len(converted) / required)
        converted = np.pad(converted[:required], (0, max(0, required - len(converted))))
        partial = output.with_suffix(".partial.wav")
        event("Writing aligned voice audio", 0.97)
        sf.write(partial, np.clip(converted, -1, 1), wrapper.sr, subtype="PCM_16")
        partial.replace(output)
        del wrapper
        torch.cuda.empty_cache()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    event("Voice change complete", 1.0, result=str(output))


def main() -> int:
    request = json.loads(sys.stdin.readline())
    try:
        if request["action"] == "setup":
            setup(request)
        elif request["action"] == "generate":
            generate(request)
        else:
            raise ValueError("Unknown SeedVC action")
        return 0
    except Exception as exc:
        traceback.print_exc(file=sys.stderr)
        event("Failed", 0, error=f"{type(exc).__name__}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
