param([Parameter(Mandatory = $true)][string]$Root)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$runtime = Join-Path $Root 'parakeet-tdt-v3\runtime'
$models = Join-Path $Root 'parakeet-tdt-v3\models'
$model = Join-Path $models 'parakeet-tdt-0.6b-v3.q8_0.gguf'
$expectedSha = 'e3880d0aaaaf2c308ea2c35016b2b895c423eb3fda924c1b463d1c19b7f4d32e'
$installer = Join-Path ([IO.Path]::GetTempPath()) ('datacut-nemo-' + [guid]::NewGuid().ToString('N') + '.ps1')

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Download-Model([string]$Uri, [string]$Destination) {
    $request = [Net.WebRequest]::Create($Uri)
    $request.Timeout = 60000
    $request.ReadWriteTimeout = 300000
    $response = $request.GetResponse()
    $inputStream = $response.GetResponseStream()
    $outputStream = [IO.File]::Create($Destination)
    try {
        $buffer = New-Object byte[] (1024 * 1024)
        [long]$downloaded = 0
        $lastPercent = -1
        while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $outputStream.Write($buffer, 0, $count)
            $downloaded += $count
            if ($response.ContentLength -gt 0) {
                $percent = [int][Math]::Floor(100.0 * $downloaded / $response.ContentLength)
                if ($percent -gt $lastPercent) {
                    $lastPercent = $percent
                    $fraction = (0.18 + 0.72 * [Math]::Min(100, $percent) / 100.0)
                    Write-Output ('PROGRESS:' + $fraction.ToString('F4', [Globalization.CultureInfo]::InvariantCulture))
                }
            }
        }
    } finally {
        $outputStream.Dispose()
        $inputStream.Dispose()
        $response.Dispose()
    }
}

try {
    Write-Output 'STAGE:Downloading Windows speech runtime'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/NVIDIA/NeMo-Speech.cpp/e59dfb0b5c22655620f41391cc9a000d6f56b802/scripts/install.ps1' -OutFile $installer
    Write-Output 'PROGRESS:0.03'
    Write-Output 'STAGE:Installing isolated CPU runtime'
    & $installer -Prefix $runtime -Backend cpu -BinaryOnly -NoModifyPath
    $binary = Join-Path $runtime 'bin\nemo-speech.exe'
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Runtime executable was not installed' }
    $native = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\native'))
    if (Test-Path -LiteralPath $native -PathType Container) {
        Get-ChildItem -LiteralPath $native -Filter '*.dll' -File | ForEach-Object {
            $destination = Join-Path (Split-Path -Parent $binary) $_.Name
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                Copy-Item -LiteralPath $_.FullName -Destination $destination
            }
        }
    }
    Write-Output 'STAGE:Checking native speech runtime'
    & $binary --version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Native speech runtime could not start (exit $LASTEXITCODE)." }
    Write-Output 'PROGRESS:0.18'

    New-Item -ItemType Directory -Force -Path $models | Out-Null
    $validModel = (Test-Path -LiteralPath $model -PathType Leaf) -and
        ((Get-Sha256 $model) -eq $expectedSha)
    if (-not $validModel) {
        $partial = "$model.partial"
        Write-Output 'STAGE:Downloading Parakeet model (714 MB)'
        Download-Model 'https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3/resolve/main/parakeet-tdt-0.6b-v3.q8_0.gguf' $partial
        Write-Output 'STAGE:Verifying model SHA-256'
        Write-Output 'PROGRESS:0.90'
        $actual = Get-Sha256 $partial
        if ($actual -ne $expectedSha) { throw 'Downloaded model failed SHA-256 verification' }
        Move-Item -LiteralPath $partial -Destination $model -Force
    }
    Write-Output 'PROGRESS:1.0'
    Write-Output 'STAGE:Ready'
} catch {
    Write-Error $_
    exit 1
} finally {
    Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
}
