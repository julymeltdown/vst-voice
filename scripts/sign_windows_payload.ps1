param(
  [Parameter(Mandatory=$true)][string]$PayloadRoot,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $env:WINDOWS_SIGN_CERT_SHA1) { throw 'WINDOWS_SIGN_CERT_SHA1 is required; release signing fails closed' }
if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) { throw 'signtool.exe is required' }
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$targets = @()
$clap = Join-Path $PayloadRoot 'CLAP\ProjectSEAMEditor.clap'
if (Test-Path $clap -PathType Leaf) { $targets += $clap }
$vst3 = Get-ChildItem (Join-Path $PayloadRoot 'VST3') -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Extension -in @('.vst3','.dll') }
$targets += $vst3.FullName
if ($targets.Count -eq 0) { throw 'No Windows CLAP/VST3 binaries found to sign' }
foreach ($target in $targets) {
  & signtool.exe sign /sha1 $env:WINDOWS_SIGN_CERT_SHA1 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $target
  if ($LASTEXITCODE -ne 0) { throw "Signing failed: $target" }
  & signtool.exe verify /pa /all /v $target 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory ((Split-Path $target -Leaf)+'.verify.log'))
  if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $target" }
}
python (Join-Path $PSScriptRoot 'refresh_phase13a_wrapper_manifests.py') $PayloadRoot --platform windows
if ($LASTEXITCODE -ne 0) { throw 'Signed Windows wrapper manifest refresh failed' }
@{schemaVersion=1;status='PASS';signedFiles=$targets;timestamped=$true} |
  ConvertTo-Json -Depth 5 | Set-Content (Join-Path $EvidenceDirectory 'result.json') -Encoding UTF8
