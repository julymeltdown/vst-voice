param(
  [Parameter(Mandatory=$true)][string]$PayloadRoot,
  [Parameter(Mandatory=$true)][string]$FilePath,
  [string]$VerificationLog,
  [switch]$DevelopmentOnly
)
$ErrorActionPreference = 'Stop'
$certificate = $env:WINDOWS_DEVELOPMENT_SIGN_CERT_SHA1
if (-not $DevelopmentOnly) {
  $root=Resolve-Path (Join-Path $PSScriptRoot '..')
  & python (Join-Path $root 'scripts\verify_production_signing_input.py') --payload $PayloadRoot
  if ($LASTEXITCODE -ne 0) { throw 'File signing input is not production-eligible' }
  $certificate = $env:WINDOWS_SIGN_CERT_SHA1
}
if (-not $certificate) { throw 'The selected Windows signing certificate is required' }
if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) { throw 'signtool.exe is required' }
if (-not (Test-Path $FilePath -PathType Leaf)) { throw "Signing target is missing: $FilePath" }
& signtool.exe sign /sha1 $certificate /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $FilePath
if ($LASTEXITCODE -ne 0) { throw "Signing failed: $FilePath" }
if ($VerificationLog) {
  & signtool.exe verify /pa /all /v $FilePath 2>&1 | Tee-Object -FilePath $VerificationLog
} else {
  & signtool.exe verify /pa /all /v $FilePath
}
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $FilePath" }
