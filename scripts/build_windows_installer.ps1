param(
  [Parameter(Mandatory=$true)][string]$PayloadRoot,
  [Parameter(Mandatory=$true)][string]$OutputInstaller
)
$ErrorActionPreference='Stop'
if (-not (Get-Command makensis.exe -ErrorAction SilentlyContinue)) { throw 'NSIS 3.12 makensis.exe is required' }
$root=Resolve-Path (Join-Path $PSScriptRoot '..')
$required=@(
  'CLAP\ProjectSEAMEditor.clap',
  'CLAP\ProjectSEAMEditor.resources',
  'VST3\ProjectSEAMEditor.vst3',
  'THIRD_PARTY_NOTICES.md',
  'SBOM.spdx.json'
)
foreach($item in $required){if(-not(Test-Path(Join-Path $PayloadRoot $item))){throw "Missing payload: $item"}}
if ((Get-Item (Join-Path $PayloadRoot 'VST3\ProjectSEAMEditor.vst3')).PSIsContainer) {
  throw 'Windows Phase 13A installer requires the audited single-file VST3 wrapper build'
}
$versionOutput = & makensis.exe /VERSION
if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch '^v?3\.12') { throw "NSIS 3.12 is required, found: $versionOutput" }
$escapedPayload=(Resolve-Path $PayloadRoot).Path
$outputPath=[System.IO.Path]::GetFullPath($OutputInstaller)
New-Item -ItemType Directory -Force ([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null
& makensis.exe /V4 "/DPAYLOAD_ROOT=$escapedPayload" "/DOUTPUT_EXE=$outputPath" (Join-Path $root 'packaging\windows\ProjectSEAM.nsi')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(-not(Test-Path $outputPath -PathType Leaf)){throw 'NSIS did not create the installer'}
