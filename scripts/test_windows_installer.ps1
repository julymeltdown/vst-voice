param(
  [Parameter(Mandatory=$true)][string]$Installer,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
if(-not(Test-Path $Installer -PathType Leaf)){throw "Installer missing: $Installer"}
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$started=(Get-Date).ToUniversalTime().ToString('o')
# First install and same-version reinstall/update path.
& $Installer /S
if($LASTEXITCODE -ne 0){throw "Installer failed: $LASTEXITCODE"}
& $Installer /S
if($LASTEXITCODE -ne 0){throw "Same-version reinstall failed: $LASTEXITCODE"}
$clap="$env:CommonProgramFiles\CLAP\ProjectSEAMEditor.clap"
$resources="$env:CommonProgramFiles\CLAP\ProjectSEAMEditor.resources"
$vst3="$env:CommonProgramFiles\VST3\ProjectSEAMEditor.vst3"
$uninstaller="$env:ProgramData\ProjectSEAM\Uninstall.exe"
$documentation="$env:ProgramData\ProjectSEAM\Documentation\external-beta-documentation.json"
foreach($path in @($clap,$resources,$vst3,$uninstaller,$documentation)){if(-not(Test-Path $path)){throw "Install missing: $path"}}
& $uninstaller /S
if($LASTEXITCODE -ne 0){throw "Uninstaller failed: $LASTEXITCODE"}
foreach($path in @($clap,$resources,$vst3)){if(Test-Path $path){throw "Uninstall left: $path"}}
@{
  schemaVersion=1
  status='PASS'
  platform='windows'
  executedAt=$started
  installer=(Resolve-Path $Installer).Path
  checks=@{
    cleanInstall='PASS'
    sameVersionReinstall='PASS'
    clapInstalled='PASS'
    clapResourcesInstalled='PASS'
    vst3Installed='PASS'
    uninstall='PASS'
    residualFiles='PASS'
  }
} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $EvidenceDirectory 'result.json') -Encoding UTF8
Write-Host 'WINDOWS_CLEAN_INSTALL_UPDATE_UNINSTALL=PASS'
