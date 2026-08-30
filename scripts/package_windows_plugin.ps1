param(
  [Parameter(Mandatory=$true)][string]$ClapBinary,
  [Parameter(Mandatory=$true)][string]$OutputZip
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$stage = Join-Path $env:TEMP 'ProjectSEAMEditor-package'
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $ClapBinary (Join-Path $stage 'ProjectSEAMEditor.clap')
Copy-Item (Join-Path $root 'packaging\windows\install-project-seam.ps1') $stage
Copy-Item (Join-Path $root 'packaging\windows\uninstall-project-seam.ps1') $stage
New-Item -ItemType Directory -Force (Join-Path $stage 'ProjectSEAMEditor.resources') | Out-Null
Copy-Item (Join-Path $root 'packaging\release-resource-inventory.json') (Join-Path $stage 'ProjectSEAMEditor.resources\release-resource-inventory.json')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $OutputZip -Force
