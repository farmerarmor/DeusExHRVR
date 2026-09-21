param([Parameter(Mandatory=$true)][string]$GameDirectory)
$ErrorActionPreference='Stop'
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$exe=Join-Path $gameRoot 'DXHRDC.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'DXHRDC.exe not found.'}
if(Get-Process DXHRDC -ErrorAction SilentlyContinue){throw 'Close Deus Ex before installation.'}
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne '8266B6B4A5BF25F2F4E8DE068AA3720F6289C962BB1C2BB70A7B1C111BA510A1'){throw 'This build is staged for the inspected Steam DXHRDC 2.0.66.0 executable only.'}
$payload=Join-Path $PSScriptRoot 'dist'
$files=@('d3d11.dll','atidxx32.dll','atiadlxy.dll','DeusExHRVR\DeusExHRVRHost.exe')
$shaderRoot=Join-Path $payload 'DeusExHRVR\shaders\dxhr'
if(Test-Path -LiteralPath (Join-Path $shaderRoot 'table.csv')){
    $files+='DeusExHRVR\shaders\dxhr\table.csv'
    foreach($shader in Get-ChildItem -LiteralPath (Join-Path $shaderRoot 'compiled') -Filter '*.cso' -File){
        if($shader.Name -notmatch '^[A-Za-z0-9_. -]+\.cso$'){throw 'Unexpected shader filename.'}
        $files+='DeusExHRVR\shaders\dxhr\compiled\'+$shader.Name
    }
}
foreach($f in $files){if(!(Test-Path -LiteralPath (Join-Path $payload $f))){throw "Missing payload: $f"}}
$backupRoot=Join-Path $gameRoot 'DeusExHRVR-backup'
$manifestPath=Join-Path $backupRoot 'install.json'
$regPath='HKCU:\Software\Eidos\Deus Ex: HRDC\Graphics'
$changes=@{EnableDirectX11=1;StereoMode=1;EnableVSync=0;AntiAliasingMode=0}
if(!(Test-Path -LiteralPath $manifestPath)) {
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $reg=Get-Item -LiteralPath $regPath
    $entries=@();$settings=@()
    foreach($f in $files){
        $target=Join-Path $gameRoot $f;$exists=Test-Path -LiteralPath $target
        if($exists){$dest=Join-Path $backupRoot $f;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null;Copy-Item -LiteralPath $target -Destination $dest}
        $entries+=@{path=$f;existed=$exists}
    }
    foreach($key in $changes.Keys){$exists=$reg.GetValueNames() -contains $key;$settings+=@{name=$key;existed=$exists;value=if($exists){$reg.GetValue($key)}else{$null}}}
    @{version=1;gameRoot=$gameRoot;files=$entries;settings=$settings} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
# Extend an older installation's backup before installing newly added payloads.
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if($manifest.gameRoot -ne $gameRoot){throw 'Backup belongs to another game directory.'}
foreach($f in $files){
    if($f -in @($manifest.files.path)){continue}
    $target=Join-Path $gameRoot $f;$exists=Test-Path -LiteralPath $target
    if($exists){$dest=Join-Path $backupRoot $f;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null;Copy-Item -LiteralPath $target -Destination $dest}
    $manifest.files=@($manifest.files)+@([pscustomobject]@{path=$f;existed=$exists})
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
foreach($f in $files){$target=Join-Path $gameRoot $f;New-Item -ItemType Directory -Path (Split-Path $target) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $payload $f) -Destination $target -Force}
foreach($key in $changes.Keys){New-ItemProperty -LiteralPath $regPath -Name $key -Value $changes[$key] -PropertyType DWord -Force|Out-Null}
'Installed native stereo bridge. Original files/settings are in '+$backupRoot
