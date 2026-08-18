#!/usr/bin/env bash
set -euo pipefail
pkg="${1:?pkg required}"
evidence="${2:?evidence directory required}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 2; }
[[ -f "$pkg" ]] || { echo "missing pkg: $pkg" >&2; exit 3; }
mkdir -p "$evidence"
started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
sudo installer -pkg "$pkg" -target / 2>&1 | tee "$evidence/install.log"
# Repeat installation to exercise the same-version update path.
sudo installer -pkg "$pkg" -target / 2>&1 | tee "$evidence/reinstall.log"
test -e /Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap
test -e /Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3
test -e /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component
test -f "/Library/Application Support/ProjectSEAM/THIRD_PARTY_NOTICES.md"
pkgutil --pkg-info com.project-seam.plugins > "$evidence/pkgutil.log"
pluginkit -a /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component > "$evidence/pluginkit.log" 2>&1 || true
sudo "/Library/Application Support/ProjectSEAM/uninstall_macos_plugins.sh" 2>&1 | tee "$evidence/uninstall.log"
test ! -e /Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap
test ! -e /Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3
test ! -e /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component
python3 - "$evidence/result.json" "$pkg" "$started" <<'PY'
import json,sys
json.dump({
  'schemaVersion':1,
  'status':'PASS',
  'platform':'macos',
  'installer':sys.argv[2],
  'executedAt':sys.argv[3],
  'checks':{
    'cleanInstall':'PASS',
    'sameVersionReinstall':'PASS',
    'clapInstalled':'PASS',
    'vst3Installed':'PASS',
    'auv2Installed':'PASS',
    'uninstall':'PASS',
    'residualFiles':'PASS'
  }
},open(sys.argv[1],'w'),indent=2)
PY
echo 'MACOS_CLEAN_INSTALL_UPDATE_UNINSTALL=PASS'
