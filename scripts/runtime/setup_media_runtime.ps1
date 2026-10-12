$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
try {
    $request = [Console]::In.ReadLine() | ConvertFrom-Json
    $runtime = [string]$request.runtime
    $kind = [string]$request.kind
    if ($kind -notin @('image', 'video', 'music', 'sound')) { throw 'Unknown generation runtime.' }
    if (-not [IO.Path]::IsPathRooted($runtime)) { throw 'Runtime path must be absolute.' }
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null
    if ($kind -eq 'music') {
        $driveRoot = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($runtime))
        $drive = [IO.DriveInfo]::new($driveRoot)
        $minimumFree = 25GB
        if ($drive.AvailableFreeSpace -lt $minimumFree -and -not (Test-Path -LiteralPath (Join-Path $runtime '.datacut-verified.json'))) {
            $available = [math]::Round($drive.AvailableFreeSpace / 1GB, 1)
            throw "Music setup needs about 25 GiB free on $driveRoot. Available: $available GiB. Choose another runtime folder in Settings > Generative > Media > Music."
        }
    }
    $bootstrap = Join-Path $runtime 'bootstrap'
    New-Item -ItemType Directory -Force -Path $bootstrap | Out-Null
    $uv = Join-Path $bootstrap 'uv.exe'
    if (-not (Test-Path -LiteralPath $uv -PathType Leaf)) {
        Write-Output '{"stage":"Downloading private Python installer","progress":0.02}'
        $release = 'https://releases.astral.sh/github/uv/releases/download/0.12.19/uv-x86_64-pc-windows-msvc.zip'
        $archive = Join-Path $bootstrap 'uv.zip'
        $checksum = Join-Path $bootstrap 'uv.zip.sha256'
        Invoke-WebRequest -UseBasicParsing -Uri $release -OutFile $archive
        Invoke-WebRequest -UseBasicParsing -Uri ($release + '.sha256') -OutFile $checksum
        $expected = ((Get-Content -LiteralPath $checksum -Raw).Trim() -split '\s+')[0]
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $expected) {
            throw 'Private Python installer failed its SHA-256 check.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $bootstrap -Force
        Remove-Item -LiteralPath $archive, $checksum -Force
    }
    $env:UV_PYTHON_INSTALL_DIR = Join-Path $runtime 'python'
    $env:UV_CACHE_DIR = Join-Path $runtime 'cache'
    $env:UV_PYTHON_NO_REGISTRY = '1'
    $env:UV_NO_PROGRESS = '1'
    # Cache and environment share the runtime drive; uv uses hardlinks on Windows.
    Remove-Item Env:UV_LINK_MODE -ErrorAction SilentlyContinue
    $env:PYTHONNOUSERSITE = '1'
    $version = if ($kind -eq 'sound') { '3.10' } else { '3.11' }
    $python = Join-Path $runtime '.venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        Write-Output ('{"stage":"Installing private Python ' + $version + '","progress":0.07}')
        & $uv python install $version --install-dir $env:UV_PYTHON_INSTALL_DIR --no-bin
        if ($LASTEXITCODE -ne 0) { throw "Could not install private Python $version." }
        & $uv venv (Join-Path $runtime '.venv') --python $version --managed-python
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the private Python environment.' }
    }
    $worker = Join-Path $PSScriptRoot 'setup_media_runtime.py'
    if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) { throw 'Media setup worker is missing from the app installation.' }
    $payload = $request | ConvertTo-Json -Compress -Depth 16
    $payload | & $python -u $worker $uv
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} catch {
    $message = $_.Exception.Message | ConvertTo-Json -Compress
    Write-Output ('{"stage":"Failed","progress":0,"error":' + $message + '}')
    exit 1
}
