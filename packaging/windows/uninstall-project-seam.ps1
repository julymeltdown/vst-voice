$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:COMMONPROGRAMFILES 'CLAP\ProjectSEAMEditor.clap'
Remove-Item $destination -Force -ErrorAction SilentlyContinue
Write-Host "Removed $destination"
