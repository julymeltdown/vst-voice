#!/usr/bin/env bash
set -euo pipefail
payload="${1:?payload root required}"
evidence="${2:?evidence directory required}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS signing must run on macOS' >&2; exit 2; }
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
payload="$(python3 "$script_root/scripts/verify_release_payload_manifest.py" \
  --payload "$payload" --platform macos-arm64 --field root)"
python3 "$script_root/scripts/verify_production_signing_input.py" --payload "$payload"
identity="${APPLE_DEVELOPER_ID_APPLICATION:?APPLE_DEVELOPER_ID_APPLICATION is required; signing fails closed}"
mkdir -p "$evidence"
items=(
  "$payload/Standalone/Project SEAM.app"
  "$payload/Tools/seam_installer_verifier"
  "$payload/CLAP/ProjectSEAMEditor.clap"
  "$payload/VST3/ProjectSEAMEditor.vst3"
  "$payload/AU/ProjectSEAMEditor.component"
)
wrappers=(
  "$payload/VST3/ProjectSEAMEditor.vst3"
  "$payload/AU/ProjectSEAMEditor.component"
)
for item in "${items[@]}"; do
  [[ -e "$item" ]] || { echo "missing $item" >&2; exit 3; }
  find "$item" -type f -perm -111 -print0 | while IFS= read -r -d '' binary; do
    codesign --force --options runtime --timestamp --sign "$identity" "$binary"
  done
  codesign --force --deep --options runtime --timestamp --sign "$identity" "$item"
done
python3 "$script_root/scripts/refresh_phase13a_wrapper_manifests.py" "$payload"
for item in "${wrappers[@]}"; do
  codesign --force --deep --options runtime --timestamp --sign "$identity" "$item"
done
for item in "${items[@]}"; do
  codesign --verify --deep --strict --verbose=4 "$item" 2>&1 | tee "$evidence/$(basename "$item").codesign.log"
done
python3 "$script_root/scripts/verify_release_dependency_closure.py" \
  --payload "$payload" --source-root "$script_root" --platform macos-arm64
python3 "$script_root/scripts/assemble_release_payload.py" \
  --payload "$payload" --source-root "$script_root" --platform macos-arm64
python3 - "$evidence/result.json" <<'PY'
import json,sys
json.dump({'schemaVersion':1,'status':'PASS','nestedSigning':True,'hardenedRuntime':True,'timestamped':True,'gatekeeperAssessment':'DEFERRED_TO_NOTARIZED_PKG'},open(sys.argv[1],'w'),indent=2)
PY
