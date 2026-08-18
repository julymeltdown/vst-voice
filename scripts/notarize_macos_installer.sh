#!/usr/bin/env bash
set -euo pipefail
pkg="${1:?signed pkg required}"
evidence="${2:?evidence directory required}"
profile="${APPLE_NOTARY_PROFILE:?APPLE_NOTARY_PROFILE is required; notarization fails closed}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS notarization must run on macOS' >&2; exit 2; }
[[ -f "$pkg" ]] || { echo "missing pkg: $pkg" >&2; exit 3; }
mkdir -p "$evidence"
xcrun notarytool submit "$pkg" --keychain-profile "$profile" --wait --output-format json | tee "$evidence/notarytool.json"
python3 - "$evidence/notarytool.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
if value.get('status') != 'Accepted':
    raise SystemExit('notarization status is not Accepted')
PY
xcrun stapler staple "$pkg" 2>&1 | tee "$evidence/stapler.log"
xcrun stapler validate "$pkg" 2>&1 | tee -a "$evidence/stapler.log"
spctl --assess --type install --verbose=4 "$pkg" 2>&1 | tee "$evidence/spctl-install.log"
