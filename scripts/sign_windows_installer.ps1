param(
  [Parameter(Mandatory=$true)][string]$Installer,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
if (-not $env:WINDOWS_SIGN_CERT_SHA1) { throw 'WINDOWS_SIGN_CERT_SHA1 is required; installer signing fails closed' }
if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) { throw 'signtool.exe is required' }
if (-not (Test-Path $Installer -PathType Leaf)) { throw "Installer does not exist: $Installer" }
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
& signtool.exe sign /sha1 $env:WINDOWS_SIGN_CERT_SHA1 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Installer
if ($LASTEXITCODE -ne 0) { throw 'Installer signing failed' }
& signtool.exe verify /pa /all /v $Installer 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory 'installer.verify.log')
if ($LASTEXITCODE -ne 0) { throw 'Installer signature verification failed' }
@{schemaVersion=1;status='PASS';artifact=$Installer;timestamped=$true} |
  ConvertTo-Json -Depth 5 | Set-Content (Join-Path $EvidenceDirectory 'result.json') -Encoding UTF8
