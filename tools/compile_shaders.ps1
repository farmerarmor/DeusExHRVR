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
    [string]$ShaderDir,
    [string]$OutDir
)
# Resolve paths from $PSScriptRoot (the tools/ dir) in the script body, not
# in param defaults: $PSScriptRoot can be empty during param evaluation in
# some PowerShell hosting contexts (e.g. pwsh -File in CI).
if (-not $ShaderDir) {
    $RepoRoot = Split-Path $PSScriptRoot -Parent
    $ShaderDir = Join-Path $RepoRoot "shaders\dxhr"
}
if (-not $OutDir) { $OutDir = Join-Path $ShaderDir "compiled" }

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
    # Hash-keyed replacement shaders (0x........ in filename): one .cso, entry 'main'.
    if ($hashRe.IsMatch($file.Name)) {
        $m = $profileRe.Match($file.Name)
        if (-not $m) { $skip++; Write-Host "skip  $($file.Name) (no profile)"; return }
        $profile = "$($m.Groups[1].Value)_$($m.Groups[2].Value)"
        $csoName = $file.Name -replace '\.hlsl$','.cso'
        $csoPath = Join-Path $OutDir $csoName
        $fa = @('/T', $profile, '/E', 'main', '/O3', '/nologo',
                '/I', $ShaderDir,
                '/I', (Split-Path $ShaderDir -Parent),
                '/Fo', $csoPath, $file.FullName)
        & fxc @fa
        if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\$csoName" }
        else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
        return
    }

    # Injected-pass shaders (no 0x hash). These compile to multiple .cso files,
    # one per entry point + macro combination. The .cso stem encodes the entry
    # point and macro so LumaPasses::Load can find each variant.
    switch ($file.Name) {
        'Luma_DXHR_XeGTAO.hlsl' {
            # 4 CS variants: prefilter, main, denoise(FINAL_APPLY=0), denoise(=1)
            $variants = @(
                @{ Entry = 'prefilter_depths16x16_cs'; Stem = 'Luma_DXHR_XeGTAO_prefilter_depths16x16_cs'; Macros = @() },
                @{ Entry = 'main_pass_cs'; Stem = 'Luma_DXHR_XeGTAO_main_pass_cs'; Macros = @() },
                @{ Entry = 'denoise_pass_cs'; Stem = 'Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_0'; Macros = @('/DXE_GTAO_FINAL_APPLY=0') },
                @{ Entry = 'denoise_pass_cs'; Stem = 'Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_1'; Macros = @('/DXE_GTAO_FINAL_APPLY=1') }
            )
            foreach ($v in $variants) {
                $csoPath = Join-Path $OutDir "$($v.Stem).cso"
                $fa = @('/T', 'cs_5_0', '/E', $v.Entry, '/O3', '/nologo',
                        '/I', $ShaderDir,
                        '/I', (Split-Path $ShaderDir -Parent)) + $v.Macros + @('/Fo', $csoPath, $file.FullName)
                & fxc @fa
                if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) [$($v.Entry)/$($v.Stem)] -> compiled\$($v.Stem).cso" }
                else { $fail++; Write-Host "FAIL  $($file.Name) [$($v.Entry)/$($v.Stem)] (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
            }
            return
        }
        'Luma_SMAA_Linearize.hlsl' {
            $csoPath = Join-Path $OutDir 'Luma_SMAA_Linearize.cso'
            $fa = @('/T', 'cs_5_0', '/E', 'main', '/O3', '/nologo',
                    '/I', $ShaderDir, '/I', (Split-Path $ShaderDir -Parent),
                    '/Fo', $csoPath, $file.FullName)
            & fxc @fa
            if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\Luma_SMAA_Linearize.cso" }
            else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
            return
        }
        'Luma_ModulateLighting.hlsl' {
            $csoPath = Join-Path $OutDir 'Luma_ModulateLighting.cso'
            $fa = @('/T', 'ps_5_0', '/E', 'main', '/O3', '/nologo',
                    '/I', $ShaderDir, '/I', (Split-Path $ShaderDir -Parent),
                    '/Fo', $csoPath, $file.FullName)
            & fxc @fa
            if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\Luma_ModulateLighting.cso" }
            else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
            return
        }
        'Luma_Copy_VS.hlsl' {
            # Fullscreen-triangle VS used by DrawCustomPixelShader (ModulateLighting + SMAA).
            $csoPath = Join-Path $OutDir 'Luma_Copy_VS.cso'
            $fa = @('/T', 'vs_5_0', '/E', 'main', '/O3', '/nologo',
                    '/I', $ShaderDir, '/I', (Split-Path $ShaderDir -Parent),
                    '/Fo', $csoPath, $file.FullName)
            & fxc @fa
            if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\Luma_Copy_VS.cso" }
            else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
            return
        }
        'Luma_Copy_PS.hlsl' {
            # Optional copy PS (not used by current passes, but compile to verify syntax).
            $csoPath = Join-Path $OutDir 'Luma_Copy_PS.cso'
            $fa = @('/T', 'ps_5_0', '/E', 'main', '/O3', '/nologo',
                    '/I', $ShaderDir, '/I', (Split-Path $ShaderDir -Parent),
                    '/Fo', $csoPath, $file.FullName)
            & fxc @fa
            if ($LASTEXITCODE -eq 0) { $ok++; Write-Host "ok    $($file.Name) -> compiled\Luma_Copy_PS.cso" }
            else { $fail++; Write-Host "FAIL  $($file.Name) (fxc exit $LASTEXITCODE)" -ForegroundColor Red }
            return
        }
        default {
            # Luma_SMAA_impl.hlsl is an include library, not directly compiled.
            $skip++; Write-Host "skip  $($file.Name) (include library)"; return
        }
    }
}

Write-Host ""
Write-Host "Shader compile: $ok ok, $fail failed, $skip skipped"
if ($fail -gt 0) { exit 1 }
