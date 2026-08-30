param(
  [Parameter(Mandatory=$true)][string]$PayloadRoot,
  [Parameter(Mandatory=$true)][string]$OutputInstaller,
  [string]$ProductVersion = $env:PROJECT_SEAM_VERSION,
  [string]$BuildId = $env:SEAM_BUILD_ID,
  [string]$SourceCommit = $env:SEAM_SOURCE_COMMIT
)
$ErrorActionPreference='Stop'
if (-not (Get-Command makensis.exe -ErrorAction SilentlyContinue)) { throw 'NSIS 3.12 makensis.exe is required' }
$root=Resolve-Path (Join-Path $PSScriptRoot '..')
$required=@(
  'Standalone\seam_editor_native.exe',
  'Standalone\Resources',
  'Standalone\Resources\release-resource-inventory.json',
  'Standalone\Resources\Manual\EULA.md',
  'Standalone\Resources\Manual\PRIVACY.md',
  'Standalone\Resources\Manual\QUICK_START.md',
  'Standalone\Resources\Manual\USER_MANUAL.md',
  'Standalone\Resources\Manual\KNOWN_LIMITATIONS.md',
  'Standalone\Resources\Manual\UPDATE_AND_ROLLBACK.md',
  'Standalone\Resources\Manual\BETA_TESTER_CHECKLIST.md',
  'Standalone\Resources\Manual\Support\SUPPORT.md',
  'Standalone\Resources\Manual\Support\SECURITY_RESPONSE.md',
  'Standalone\Resources\Manual\external-beta-documentation.json',
  'RELEASE_IDENTITY.json',
  'CLAP\ProjectSEAMEditor.clap',
  'CLAP\ProjectSEAMEditor.resources',
  'CLAP\ProjectSEAMEditor.resources\release-resource-inventory.json',
  'VST3\ProjectSEAMEditor.vst3',
  'THIRD_PARTY_NOTICES.md',
  'SBOM.spdx.json',
  'Documentation\external-beta-documentation.json'
)
foreach($item in $required){if(-not(Test-Path(Join-Path $PayloadRoot $item))){throw "Missing payload: $item"}}
$identity = Get-Content (Join-Path $PayloadRoot 'RELEASE_IDENTITY.json') -Raw | ConvertFrom-Json
$payloadVersion = [string]$identity.version
$payloadBuildId = [string]$identity.buildId
$payloadSourceCommit = [string]$identity.sourceCommit
if ($payloadVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Payload release version must use strict semantic versioning' }
if ([string]::IsNullOrWhiteSpace($payloadBuildId)) { throw 'Payload buildId is required' }
if ($payloadSourceCommit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Payload sourceCommit must be a 40-character hexadecimal commit' }
if ($ProductVersion -and $ProductVersion -ne $payloadVersion) { throw 'ProductVersion does not match payload release identity' }
if ($BuildId -and $BuildId -ne $payloadBuildId) { throw 'BuildId does not match payload release identity' }
if ($SourceCommit -and $SourceCommit -ne $payloadSourceCommit) { throw 'SourceCommit does not match payload release identity' }
$ProductVersion = $payloadVersion
$BuildId = $payloadBuildId
$SourceCommit = $payloadSourceCommit
if (-not (Get-Item (Join-Path $PayloadRoot 'VST3\ProjectSEAMEditor.vst3')).PSIsContainer) { throw 'Windows Phase 13A installer requires a package-shaped VST3 folder' }
if (-not (Test-Path (Join-Path $PayloadRoot 'VST3\ProjectSEAMEditor.vst3\moduleinfo.json'))) { throw 'Windows Phase 13A VST3 package is missing moduleinfo.json' }
$versionOutput = & makensis.exe /VERSION
if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch '^v?3\.12') { throw "NSIS 3.12 is required, found: $versionOutput" }
$escapedPayload=(Resolve-Path $PayloadRoot).Path
$outputPath=[System.IO.Path]::GetFullPath($OutputInstaller)
New-Item -ItemType Directory -Force ([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null
& makensis.exe /V4 "/DPAYLOAD_ROOT=$escapedPayload" "/DOUTPUT_EXE=$outputPath" "/DPRODUCT_VERSION=$ProductVersion" "/DBUILD_ID=$BuildId" "/DSOURCE_COMMIT=$SourceCommit" (Join-Path $root 'packaging\windows\ProjectSEAM.nsi')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(-not(Test-Path $outputPath -PathType Leaf)){throw 'NSIS did not create the installer'}
