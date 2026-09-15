# Compile Luma's DXHR replacement shaders to .cso blobs with fxc.
#
# Mirrors the directory layout Luma expects: shaders/dxhr/*.hlsl include
# "Includes/..." (DXHR-specific, shaders/dxhr/Includes/) and
# "../Includes/..." (global, shaders/Includes/). fxc resolves relative
# includes from the source file's directory, so both resolve naturally.
#
# Output: shaders/dxhr/compiled/*.cso, one per hash-keyed .hlsl. Injected
# passes (Luma_*) are skipped here; they are Phase 3 and compiled with
# per-entry-point macros at runtime/CI later.
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools\compile_shaders.ps1
# Requires Windows SDK fxc on PATH (set by the MSVC dev environment / CI).
[CmdletBinding()]
param(
    [string]$ShaderDir = (Resolve-Path "$PSScriptRoot\..\shaders\dxhr"),
    [string]$OutDir    = (Join-Path (Resolve-Path "$PSScriptRoot\..\shaders\dxhr") "compiled")
)

$ErrorActionPreference = 'Stop'
if (-not (Get-Command fxc -ErrorAction SilentlyContinue)) {
    throw "fxc not found on PATH. Run from a Developer Command Prompt or set up the Windows SDK."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$profileRe = [regex]'\.(ps|cs|vs)_(\d_\d)\.hlsl$'
$hashRe    = [regex]'0x[0-9A-Fa-f]{8}'
$ok = 0; $fail = 0; $skip = 0

Get-ChildItem -Path $ShaderDir -Filter '*.hlsl' | Sort-Object Name | ForEach-Object {
    $file = $_
    # Skip injected passes (no 0x hash in filename) — Phase 3.
    if (-not $hashRe.IsMatch($file.Name)) { $skip++; Write-Host "skip  $($file.Name) (no hash / injected pass)"; return }

    $m = $profileRe.Match($file.Name)
    if (-not $m) { $skip++; Write-Host "skip  $($file.Name) (no profile)"; return }
    $profile = "$($m.Groups[1].Value)_$($m.Groups[2].Value)"

    # Entry point is 'main' for all hash-keyed replacement shaders.
    $csoName = $file.Name -replace '\.hlsl$','.cso'
    $csoPath = Join-Path $OutDir $csoName

    # /O3 for performance; /WX would break on warning (too strict for a port).
    $args = @('/T', $profile, '/E', 'main', '/O3', '/nologo',
              '/I', $ShaderDir,
              '/I', (Split-Path $ShaderDir -Parent),  # shaders/ for ../Includes
              '/Fo', $csoPath, $file.FullName)
    & fxc @args
    if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\$csoName" }
    else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
}

Write-Host ""
Write-Host "Shader compile: $ok ok, $fail failed, $skip skipped"
if ($fail -gt 0) { exit 1 }
