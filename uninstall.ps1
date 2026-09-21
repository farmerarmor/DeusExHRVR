param([Parameter(Mandatory=$true)][string]$GameDirectory)
$ErrorActionPreference='Stop'
if(Get-Process DXHRDC -ErrorAction SilentlyContinue){throw 'Close Deus Ex before restoring.'}
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$backupRoot=Join-Path $gameRoot 'DeusExHRVR-backup'
$m=Get-Content -LiteralPath (Join-Path $backupRoot 'install.json') -Raw|ConvertFrom-Json
if($m.gameRoot -ne $gameRoot){throw 'Backup belongs to another game directory.'}
$allowed=@('d3d11.dll','atidxx32.dll','atiadlxy.dll','DeusExHRVR\DeusExHRVRHost.exe')
foreach($f in $m.files){
    $shader=$f.path -eq 'DeusExHRVR\shaders\dxhr\table.csv' -or $f.path -match '^DeusExHRVR\\shaders\\dxhr\\compiled\\[A-Za-z0-9_. -]+\.cso$'
    if($f.path -notin $allowed -and !$shader){throw 'Unexpected file in backup manifest.'}
    $target=Join-Path $gameRoot $f.path
    if($f.existed){Copy-Item -LiteralPath (Join-Path $backupRoot $f.path) -Destination $target -Force}
    elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
}
$regPath='HKCU:\Software\Eidos\Deus Ex: HRDC\Graphics'
foreach($s in $m.settings){
    if($s.name -notin @('EnableDirectX11','StereoMode','EnableVSync','AntiAliasingMode')){throw 'Unexpected registry setting.'}
    if($s.existed){New-ItemProperty -LiteralPath $regPath -Name $s.name -Value $s.value -PropertyType DWord -Force|Out-Null}
    else{Remove-ItemProperty -LiteralPath $regPath -Name $s.name -ErrorAction SilentlyContinue}
}
'Restored original graphics settings and files. Diagnostic logs and backup are preserved.'
