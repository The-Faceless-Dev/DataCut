$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
try {
    $request = [Console]::In.ReadLine() | ConvertFrom-Json
    $runtime = [string]$request.runtime
    if (-not [IO.Path]::IsPathRooted($runtime)) { throw 'SeedVC runtime path must be absolute.' }
    if (-not (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue)) { throw 'An NVIDIA CUDA GPU could not be detected.' }
    $gpu = & nvidia-smi.exe --query-gpu=memory.total --format=csv,noheader,nounits 2>$null
    $largest = @($gpu | ForEach-Object { [int]$_.Trim() } | Measure-Object -Maximum)[0].Maximum
    if ($largest -lt 10000) { throw "SeedVC needs a 10 GB class NVIDIA GPU; detected $largest MiB." }

    $parent = Split-Path -Parent $runtime
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $runtime 'seed_vc_wrapper.py') -PathType Leaf)) {
        Write-Output '{"stage":"Downloading SeedVC source","progress":-1}'
        if (Test-Path -LiteralPath $runtime) {
            if (@(Get-ChildItem -LiteralPath $runtime -Force).Count -ne 0) {
                throw 'SeedVC folder contains other files; choose an empty runtime folder.'
            }
        }
        New-Item -ItemType Directory -Force -Path $runtime | Out-Null
        $archive = Join-Path $runtime 'seedvc-source.zip'
        Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/Plachtaa/seed-vc/archive/51383efd921027683c89e5348211d93ff12ac2a8.zip' -OutFile $archive
        & tar.exe -xf $archive --strip-components=1 -C $runtime
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $runtime 'seed_vc_wrapper.py'))) {
            throw 'Could not extract SeedVC source.'
        }
        Remove-Item -LiteralPath $archive -Force
    }

    $bootstrap = Join-Path $runtime 'bootstrap'
    New-Item -ItemType Directory -Force -Path $bootstrap | Out-Null
    $uv = Join-Path $bootstrap 'uv.exe'
    if (-not (Test-Path -LiteralPath $uv -PathType Leaf)) {
        Write-Output '{"stage":"Downloading isolated Python installer","progress":-1}'
        $archive = Join-Path $bootstrap 'uv.zip'
        $release = 'https://releases.astral.sh/github/uv/releases/download/0.12.19/uv-x86_64-pc-windows-msvc.zip'
        $checksum = Join-Path $bootstrap 'uv.zip.sha256'
        Invoke-WebRequest -UseBasicParsing -Uri $release -OutFile $archive
        Invoke-WebRequest -UseBasicParsing -Uri ($release + '.sha256') -OutFile $checksum
        $expected = ((Get-Content -LiteralPath $checksum -Raw).Trim() -split '\s+')[0]
        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
        if ($actual -ine $expected) { throw 'Isolated Python installer failed SHA-256 verification.' }
        Expand-Archive -LiteralPath $archive -DestinationPath $bootstrap -Force
        Remove-Item -LiteralPath $archive -Force
        Remove-Item -LiteralPath $checksum -Force
    }
    $env:UV_PYTHON_INSTALL_DIR = Join-Path $runtime 'python'
    $env:UV_CACHE_DIR = Join-Path $runtime 'cache'
    $env:UV_PYTHON_NO_REGISTRY = '1'
    $env:UV_NO_PROGRESS = '1'
    $env:UV_LINK_MODE = 'copy'
    $python = Join-Path $runtime '.venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        Write-Output '{"stage":"Installing private Python 3.10","progress":-1}'
        & $uv python install 3.10 --install-dir $env:UV_PYTHON_INSTALL_DIR --no-bin
        if ($LASTEXITCODE -ne 0) { throw 'Could not install private Python 3.10.' }
        & $uv venv (Join-Path $runtime '.venv') --python 3.10 --managed-python
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the SeedVC environment.' }
    }
    Write-Output '{"stage":"Installing CUDA PyTorch","progress":-1}'
    & $uv pip install --python $python 'torch==2.4.0' 'torchaudio==2.4.0' 'torchvision==0.19.0' --index-url 'https://download.pytorch.org/whl/cu121'
    if ($LASTEXITCODE -ne 0) { throw 'Could not install CUDA PyTorch.' }
    Write-Output '{"stage":"Installing SeedVC speech dependencies","progress":-1}'
    & $uv pip install --python $python 'numpy==1.26.4' 'scipy==1.13.1' 'librosa==0.10.2' 'huggingface-hub>=0.28.1,<1' 'munch==4.0.0' 'einops==0.8.0' 'pydub==0.25.1' 'transformers==4.46.3' 'soundfile==0.12.1' 'funasr==1.1.5' 'hydra-core==1.3.2' 'pyyaml' 'descript-audio-codec==1.0.0' 'resemblyzer' 'accelerate' 'intel-openmp'
    if ($LASTEXITCODE -ne 0) { throw 'Could not install SeedVC dependencies.' }
    Write-Output '{"stage":"Checking private native libraries","progress":-1}'
    & $uv pip install --python $python 'pefile==2024.8.26' 'zstandard==0.24.0'
    if ($LASTEXITCODE -ne 0) { throw 'Could not install native dependency inspector.' }
    $native = Join-Path $PSScriptRoot 'native_runtime.py'
    $bundledNative = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\native'))
    & $python -u $native $runtime $bundledNative
    # native_runtime.py emits a structured error with the failed package or DLL.
    # Keep that message instead of replacing it with a generic setup failure.
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $env:HF_HUB_ENABLE_HF_TRANSFER = '0'
    $env:HF_HUB_CACHE = Join-Path $runtime 'checkpoints\hf_cache'
    Write-Output '{"stage":"Downloading SeedVC speech models","progress":-1}'
    $download = @'
from huggingface_hub import hf_hub_download, snapshot_download
from pathlib import Path
root = Path.cwd() / 'checkpoints'
items = [
 ('Plachta/Seed-VC', 'DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth'),
 ('Plachta/Seed-VC', 'config_dit_mel_seed_uvit_whisper_small_wavenet.yml'),
 ('Plachta/Seed-VC', 'DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema.pth'),
 ('Plachta/Seed-VC', 'config_dit_mel_seed_uvit_whisper_base_f0_44k.yml'),
 ('funasr/campplus', 'campplus_cn_common.bin'),
 ('lj1995/VoiceConversionWebUI', 'rmvpe.pt'),
]
for repo, name in items:
    hf_hub_download(repo, name, cache_dir=root)
snapshot_download('openai/whisper-small', allow_patterns=['config.json', 'model.safetensors', 'preprocessor_config.json'], cache_dir=root / 'hf_cache')
for repo in ['nvidia/bigvgan_v2_22khz_80band_256x', 'nvidia/bigvgan_v2_44khz_128band_512x']:
    snapshot_download(repo, allow_patterns=['config.json', 'bigvgan_generator.pt'], cache_dir=root / 'hf_cache')
'@
    Push-Location $runtime
    try { $download | & $python - } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'Could not download SeedVC speech models.' }
    $request.python = $python
    $worker = Join-Path $PSScriptRoot 'seedvc_voice_change.py'
    $payload = $request | ConvertTo-Json -Compress -Depth 16
    $payload | & $python -u $worker
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} catch {
    $message = $_.Exception.Message | ConvertTo-Json -Compress
    Write-Output ('{"stage":"Failed","progress":0,"error":' + $message + '}')
    exit 1
}
