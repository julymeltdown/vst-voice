#!/usr/bin/env bash
set -euo pipefail

payload="${1:?sealed macOS payload root is required}"
output="${2:?output .app or .zip is required}"
mode="${3:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
payload="$(python3 "$root/scripts/verify_release_payload_manifest.py" \
  --payload "$payload" --platform macos-arm64 --field root)"
app="$payload/Standalone/Project SEAM.app"

[[ "$(uname -s)" == "Darwin" ]] || {
  echo 'macOS is required for standalone bundle packaging' >&2
  exit 2
}
[[ -d "$app/Contents/MacOS" ]] || {
  echo "not an application bundle: $app" >&2
  exit 3
}
info="$app/Contents/Info.plist"
[[ -f "$info" ]] || { echo "missing Info.plist: $info" >&2; exit 3; }
bundle_executable="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$info")"
[[ -n "$bundle_executable" && "$bundle_executable" != */* ]] || {
  echo 'invalid CFBundleExecutable in Info.plist' >&2
  exit 4
}
executable="$app/Contents/MacOS/$bundle_executable"
[[ -x "$executable" ]] || { echo "missing executable: $executable" >&2; exit 3; }
lipo "$executable" -verify_arch arm64

for resource in \
  "$app/Contents/Resources/release-resource-inventory.json" \
  "$app/Contents/Resources/Manual/EULA.md" \
  "$app/Contents/Resources/Manual/PRIVACY.md" \
  "$app/Contents/Resources/Manual/QUICK_START.md" \
  "$app/Contents/Resources/Manual/USER_MANUAL.md" \
  "$app/Contents/Resources/Manual/KNOWN_LIMITATIONS.md" \
  "$app/Contents/Resources/Manual/UPDATE_AND_ROLLBACK.md" \
  "$app/Contents/Resources/Manual/BETA_TESTER_CHECKLIST.md" \
  "$app/Contents/Resources/Manual/Support/SUPPORT.md" \
  "$app/Contents/Resources/Manual/Support/SECURITY_RESPONSE.md" \
  "$app/Contents/Resources/Manual/external-beta-documentation.json" \
  "$app/Contents/Resources/Manual/ExternalBetaAcceptance.md" \
  "$app/Contents/Resources/THIRD_PARTY_NOTICES.md" \
  "$app/Contents/Resources/SBOM.spdx.json"; do
  [[ -f "$resource" ]] || { echo "missing bundle resource: $resource" >&2; exit 3; }
done

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info")"
build_id="$(/usr/libexec/PlistBuddy -c 'Print :ProjectSEAMBuildID' "$info")"
source_commit="$(/usr/libexec/PlistBuddy -c 'Print :ProjectSEAMSourceCommit' "$info")"
[[ "$bundle_id" == "com.project-seam.standalone" ]] || { echo 'unexpected bundle identifier' >&2; exit 4; }
[[ -n "$build_id" && "$build_id" != @* ]] || { echo 'missing build identity' >&2; exit 4; }
[[ "$source_commit" =~ ^[0-9a-fA-F]{40}$ ]] || { echo 'invalid source commit identity' >&2; exit 4; }
[[ "$source_commit" =~ ^0+$ ]] && {
  echo 'placeholder source commit identity is not packageable' >&2
  exit 4
}

if find "$app" -type f \( -name '*.seambank' -o -name '*voicebank*' \) -print -quit | grep -q .; then
  echo 'standalone application bundle must not contain a voicebank fixture' >&2
  exit 5
fi

if [[ "$mode" == "--unsigned" ]]; then
  echo 'MACOS_UNSIGNED_STANDALONE=PASS'
else
  python3 "$root/scripts/verify_production_signing_input.py" --payload "$payload"
  identity="${APPLE_DEVELOPER_ID_APPLICATION:?APPLE_DEVELOPER_ID_APPLICATION is required for signed standalone packaging}"
  codesign --force --options runtime --timestamp \
    --entitlements "$root/packaging/macos/ProjectSEAM.entitlements" \
    --sign "$identity" "$executable"
  codesign --force --options runtime --timestamp \
    --entitlements "$root/packaging/macos/ProjectSEAM.entitlements" \
    --sign "$identity" "$app"
  codesign --verify --deep --strict --verbose=2 "$app"
  entitlements="$(codesign -d --entitlements :- "$app" 2>/dev/null || true)"
  if grep -q 'com.apple.security.get-task-allow' <<<"$entitlements"; then
    echo 'get-task-allow is forbidden in the standalone bundle' >&2
    exit 6
  fi
  echo 'MACOS_SIGNED_STANDALONE=PASS'
fi

mkdir -p "$(dirname "$output")"
case "$output" in
  *.app)
    ditto "$app" "$output"
    ;;
  *.zip)
    ditto -c -k --norsrc --keepParent "$app" "$output"
    ;;
  *)
    echo 'output must end in .app or .zip' >&2
    exit 7
    ;;
esac
echo "MACOS_STANDALONE_OUTPUT=$output"
