#!/usr/bin/env bash
set -euo pipefail
payload="${1:?payload root required}"
output="${2:?output product pkg required}"
mode="${3:-}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 2; }
root="$(cd "$(dirname "$0")/.." && pwd)"
for path in CLAP/ProjectSEAMEditor.clap VST3/ProjectSEAMEditor.vst3 AU/ProjectSEAMEditor.component THIRD_PARTY_NOTICES.md SBOM.spdx.json; do
  [[ -e "$payload/$path" ]] || { echo "missing payload $path" >&2; exit 3; }
done
stage="$(mktemp -d)"; work="$(mktemp -d)"; trap 'rm -rf "$stage" "$work"' EXIT
mkdir -p \
  "$stage/Library/Audio/Plug-Ins/CLAP" \
  "$stage/Library/Audio/Plug-Ins/VST3" \
  "$stage/Library/Audio/Plug-Ins/Components" \
  "$stage/Library/Application Support/ProjectSEAM"
ditto "$payload/CLAP/ProjectSEAMEditor.clap" "$stage/Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap"
ditto "$payload/VST3/ProjectSEAMEditor.vst3" "$stage/Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3"
ditto "$payload/AU/ProjectSEAMEditor.component" "$stage/Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component"
cp "$payload/THIRD_PARTY_NOTICES.md" "$payload/SBOM.spdx.json" "$root/scripts/uninstall_macos_plugins.sh" \
  "$stage/Library/Application Support/ProjectSEAM/"
pkgbuild --root "$stage" --identifier com.project-seam.plugins --version 0.13.0 "$work/ProjectSEAM-plugins.pkg"
cp "$root/packaging/macos/Distribution.xml.in" "$work/Distribution.xml"
if [[ "$mode" == "--unsigned" ]]; then
  productbuild --distribution "$work/Distribution.xml" --package-path "$work" "$output"
  pkgutil --check-signature "$output" >"$work/unsigned-signature.log" 2>&1 || true
  echo 'MACOS_UNSIGNED_PKG=PASS'
else
  identity="${APPLE_DEVELOPER_ID_INSTALLER:?APPLE_DEVELOPER_ID_INSTALLER is required; release PKG creation fails closed}"
  productbuild --sign "$identity" --distribution "$work/Distribution.xml" --package-path "$work" "$output"
  pkgutil --check-signature "$output"
  echo 'MACOS_SIGNED_PKG=PASS'
fi
