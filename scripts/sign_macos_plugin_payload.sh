#!/usr/bin/env bash
set -euo pipefail
payload="${1:?payload root required}"
evidence="${2:?evidence directory required}"
identity="${APPLE_DEVELOPER_ID_APPLICATION:?APPLE_DEVELOPER_ID_APPLICATION is required; signing fails closed}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS signing must run on macOS' >&2; exit 2; }
mkdir -p "$evidence"
items=(
  "$payload/CLAP/ProjectSEAMEditor.clap"
  "$payload/VST3/ProjectSEAMEditor.vst3"
  "$payload/AU/ProjectSEAMEditor.component"
)
for item in "${items[@]}"; do
  [[ -e "$item" ]] || { echo "missing $item" >&2; exit 3; }
  find "$item" -type f -perm -111 -print0 | while IFS= read -r -d '' binary; do
    codesign --force --options runtime --timestamp --sign "$identity" "$binary"
  done
  codesign --force --deep --options runtime --timestamp --sign "$identity" "$item"
  codesign --verify --deep --strict --verbose=4 "$item" 2>&1 | tee "$evidence/$(basename "$item").codesign.log"
done
python3 - "$evidence/result.json" <<'PY'
import json,sys
json.dump({'schemaVersion':1,'status':'PASS','nestedSigning':True,'hardenedRuntime':True,'timestamped':True,'gatekeeperAssessment':'DEFERRED_TO_NOTARIZED_PKG'},open(sys.argv[1],'w'),indent=2)
PY
