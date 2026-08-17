param([Parameter(Mandatory=$true)][string]$Binary)
$ErrorActionPreference = 'Stop'
if (-not $env:WINDOWS_SIGN_CERT_SHA1) { throw 'WINDOWS_SIGN_CERT_SHA1 is required' }
& signtool.exe sign /sha1 $env:WINDOWS_SIGN_CERT_SHA1 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Binary
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& signtool.exe verify /pa /v $Binary
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
