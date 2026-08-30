param(
  [Parameter(Mandatory=$true)][string]$Installer,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [Parameter(Mandatory=$true)][string]$Verifier
)
$ErrorActionPreference='Stop'
if(-not(Test-Path $Installer -PathType Leaf)){throw "Installer missing: $Installer"}
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$started=(Get-Date).ToUniversalTime().ToString('o')
$root=Resolve-Path (Join-Path $PSScriptRoot '..')
$ownership=Get-Content (Join-Path $root 'packaging\windows\installer-ownership.json') -Raw | ConvertFrom-Json
function Resolve-OwnershipPath([string]$Relative) {
  $parts = $Relative -split '/', 2
  $base = switch ($parts[0]) {
    'ProgramFiles' { $env:ProgramFiles }
    'CommonFiles' { $env:CommonProgramFiles }
    'CommonAppData' { $env:ProgramData }
    'CommonStartMenu' { Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs' }
    'CommonDesktop' { Join-Path $env:Public 'Desktop' }
    default { throw "Unknown ownership path root: $($parts[0])" }
  }
  if ($parts.Count -eq 1) { return $base }
  return (Join-Path $base ($parts[1] -replace '/', '\'))
}
function New-VerifiedHandoff([string]$Name) {
  $output = Join-Path $EvidenceDirectory $Name
  & python (Join-Path $root 'scripts\create_development_installer_handoff.py') --package $Installer --platform windows-x64 --verifier $Verifier --output $output | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "Handoff creation failed: $Name" }
  return Get-Content (Join-Path $output 'handoff-result.json') -Raw | ConvertFrom-Json
}
function Invoke-VerifiedInstaller($Contract) {
  & $Contract.stagedPackage /S "/HANDOFF=$($Contract.handoff)" "/HANDOFFSHA256=$($Contract.handoffSha256)" "/MANIFEST=$($Contract.manifest)" "/POLICY=$($Contract.policy)" "/STAGINGROOT=$($Contract.stagingRoot)" "/CANDIDATE=$($Contract.candidateId)" | Out-Host
  $code = $LASTEXITCODE
  return $code
}
$first = New-VerifiedHandoff 'handoff-install'
$firstExit = Invoke-VerifiedInstaller $first
if($firstExit -ne 0){throw "Installer failed: $firstExit"}
$replayExit = Invoke-VerifiedInstaller $first
if($replayExit -eq 0){throw 'Replayed installer handoff was accepted'}
$replayVerification = (& $Verifier --handoff $first.handoff --manifest $first.manifest --policy $first.policy --staging-root $first.stagingRoot --expected-candidate $first.candidateId --expected-handoff-sha256 $first.handoffSha256 2>&1 | Out-String)
$replayVerifierExit = $LASTEXITCODE
$replayVerification | Set-Content (Join-Path $EvidenceDirectory 'replay-verifier.log') -Encoding UTF8
if($replayVerifierExit -ne 6 -or $replayVerification -notmatch 'already consumed'){throw "Replay verifier evidence was not specific: $replayVerifierExit $replayVerification"}
$second = New-VerifiedHandoff 'handoff-reinstall'
$secondExit = Invoke-VerifiedInstaller $second
if($secondExit -ne 0){throw "Same-version reinstall failed: $secondExit"}
$clap="$env:CommonProgramFiles\CLAP\ProjectSEAMEditor.clap"
$resources="$env:CommonProgramFiles\CLAP\ProjectSEAMEditor.resources"
$vst3="$env:CommonProgramFiles\VST3\ProjectSEAMEditor.vst3"
$uninstaller="$env:ProgramFiles\ProjectSEAM\Uninstall.exe"
$documentation="$env:ProgramData\ProjectSEAM\Documentation\external-beta-documentation.json"
foreach($path in @($clap,$resources,$vst3,$uninstaller,$documentation)){if(-not(Test-Path $path)){throw "Install missing: $path"}}
if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) { throw 'signtool.exe is required to verify the generated uninstaller' }
& signtool.exe verify /pa /all /v $uninstaller | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Generated uninstaller signature verification failed' }
& $uninstaller /S
if($LASTEXITCODE -ne 0){throw "Uninstaller failed: $LASTEXITCODE"}
foreach($relative in $ownership.ownedPaths){
  $path=Resolve-OwnershipPath $relative
  if(Test-Path -LiteralPath $path){throw "Uninstall left owned payload: $path"}
}
foreach($relative in $ownership.ownedShortcuts){
  $path=Resolve-OwnershipPath $relative
  if(Test-Path -LiteralPath $path){throw "Uninstall left owned shortcut: $path"}
}
foreach($relative in $ownership.ownedRegistryKeys){
  if(-not $relative.StartsWith('HKLM/')){throw "Unknown registry ownership root: $relative"}
  $path='Registry::HKEY_LOCAL_MACHINE\'+(($relative.Substring(5)) -replace '/', '\')
  if(Test-Path -LiteralPath $path){throw "Uninstall left owned registry key: $path"}
}
foreach($relative in $ownership.preservedSystemRoots){
  $path=Resolve-OwnershipPath $relative
  if(-not(Test-Path -LiteralPath $path -PathType Container)){throw "Replay state was not preserved: $path"}
}
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
    ownedPayloadRemoved='PASS'
    ownedShortcutsRemoved='PASS'
    ownedRegistryRemoved='PASS'
    replayStatePreserved='PASS'
    privilegedHandoff='PASS'
    replayIsolation='PASS'
    uninstallerSignature='PASS'
  }
} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $EvidenceDirectory 'result.json') -Encoding UTF8
Write-Host 'WINDOWS_CLEAN_INSTALL_UPDATE_UNINSTALL=PASS'
