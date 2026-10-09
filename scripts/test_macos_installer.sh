#!/usr/bin/env bash
set -euo pipefail
pkg="${1:?pkg required}"
evidence="${2:?evidence directory required}"
verifier="${3:?seam_installer_verifier required}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 2; }
[[ -f "$pkg" ]] || { echo "missing pkg: $pkg" >&2; exit 3; }
mkdir -p "$evidence"
started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
root="$(cd "$(dirname "$0")/.." && pwd)"
ownership="$root/packaging/macos/installer-ownership.json"
ownership_values() {
  python3 -c 'import json,sys; print(*json.load(open(sys.argv[1], encoding="utf-8"))[sys.argv[2]], sep="\n")' "$ownership" "$1"
}
create_handoff() {
  local name="$1"
  python3 "$root/scripts/create_development_installer_handoff.py" \
    --package "$pkg" --platform macos-arm64 --verifier "$verifier" \
    --output "$evidence/$name" >/dev/null
}
field() {
  python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))[sys.argv[2]])' "$1" "$2"
}
verified_install() {
  local result="$1"
  sudo env \
    SEAM_INSTALLER_HANDOFF="$(field "$result" handoff)" \
    SEAM_UPDATE_MANIFEST="$(field "$result" manifest)" \
    SEAM_UPDATE_POLICY="$(field "$result" policy)" \
    SEAM_UPDATE_STAGING_ROOT="$(field "$result" stagingRoot)" \
    SEAM_EXPECTED_CANDIDATE="$(field "$result" candidateId)" \
    SEAM_EXPECTED_HANDOFF_SHA256="$(field "$result" handoffSha256)" \
    /usr/sbin/installer -pkg "$(field "$result" stagedPackage)" -target /
}
create_handoff handoff-install
verified_install "$evidence/handoff-install/handoff-result.json" 2>&1 | tee "$evidence/install.log"
if verified_install "$evidence/handoff-install/handoff-result.json" >"$evidence/replay.log" 2>&1; then
  echo 'replayed installer handoff was accepted' >&2
  exit 4
fi
grep -q 'INSTALLER_HANDOFF=BLOCKED' "$evidence/replay.log"
create_handoff handoff-reinstall
verified_install "$evidence/handoff-reinstall/handoff-result.json" 2>&1 | tee "$evidence/reinstall.log"
test -d "/Applications/Project SEAM.app"
test -e /Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap
test -e /Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3
test -e /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component
test -f "/Library/Application Support/ProjectSEAM/THIRD_PARTY_NOTICES.md"
test -f "/Library/Application Support/ProjectSEAM/Documentation/external-beta-documentation.json"
standalone="/Applications/Project SEAM.app/Contents/MacOS/Project SEAM"
open -na "/Applications/Project SEAM.app"
launched=0
for _ in $(seq 1 40); do
  if pgrep -f "$standalone" >/dev/null; then
    launched=1
    break
  fi
  sleep 0.25
done
[[ "$launched" -eq 1 ]] || { echo 'installed standalone did not launch' >&2; exit 5; }
pkill -TERM -f "$standalone" 2>/dev/null || true
pkgutil --pkg-info com.project-seam.plugins > "$evidence/pkgutil.log"
pluginkit -a /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component > "$evidence/pluginkit.log" 2>&1 || true
sudo "/Library/Application Support/ProjectSEAM/uninstall_macos_plugins.sh" 2>&1 | tee "$evidence/uninstall.log"
while IFS= read -r relative; do
  [[ ! -e "/$relative" && ! -L "/$relative" ]] || {
    echo "uninstall left owned payload: /$relative" >&2
    exit 5
  }
done < <(ownership_values ownedPaths)
while IFS= read -r relative; do
  test -d "/$relative"
done < <(ownership_values preservedSystemRoots)
while IFS= read -r receipt; do
  if pkgutil --pkg-info "$receipt" >/dev/null 2>&1; then
    echo "uninstall left package receipt: $receipt" >&2
    exit 5
  fi
done < <(ownership_values ownedPackageReceipts)
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
    'standaloneLaunch':'PASS',
    'uninstall':'PASS',
    'ownedPayloadRemoved':'PASS'
    ,'ownedReceiptsRemoved':'PASS'
    ,'replayStatePreserved':'PASS'
    ,'privilegedHandoff':'PASS'
    ,'replayIsolation':'PASS'
  }
},open(sys.argv[1],'w'),indent=2)
PY
echo 'MACOS_CLEAN_INSTALL_UPDATE_UNINSTALL=PASS'
