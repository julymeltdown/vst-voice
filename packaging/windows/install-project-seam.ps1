param([string]$Source = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:COMMONPROGRAMFILES 'CLAP\ProjectSEAMEditor.clap'
New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
Copy-Item (Join-Path $Source 'ProjectSEAMEditor.clap') $destination -Force
Write-Host "Installed Project SEAM Editor to $destination"
