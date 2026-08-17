param(
  [Parameter(Mandatory=$true)][string]$ClapBinary,
  [Parameter(Mandatory=$true)][string]$OutputZip
)
$ErrorActionPreference = "Stop"
$stage = Join-Path $env:TEMP "ProjectSEAM-Phase11"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $stage "CLAP") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage "DemoVoice") | Out-Null
Copy-Item $ClapBinary (Join-Path $stage "CLAP\ProjectSEAMEditor.clap")
Copy-Item "$PSScriptRoot\..\..\assets\demo-human-voicebank-public-domain\*" \
  (Join-Path $stage "DemoVoice") -Recurse
@"
Project SEAM Phase 11
Install the CLAP file in %COMMONPROGRAMFILES%\CLAP or the current user's CLAP directory.
The bundled public-domain human voice is a technical fixture, not Official Voicebank 01.
"@ | Set-Content -Encoding UTF8 (Join-Path $stage "README.txt")
Compress-Archive -Path "$stage\*" -DestinationPath $OutputZip -Force
