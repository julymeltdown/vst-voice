#!/usr/bin/env bash
set -euo pipefail
export COPYFILE_DISABLE=1
payload="${1:?payload root required}"
output="${2:?output product pkg required}"
mode="${3:-}"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 2; }
root="$(cd "$(dirname "$0")/.." && pwd)"
payload="$(python3 "$root/scripts/verify_release_payload_manifest.py" \
  --payload "$payload" --platform macos-arm64 --field root)"
version="$(python3 "$root/scripts/verify_release_payload_manifest.py" \
  --payload "$payload" --platform macos-arm64 --field version)"
for path in \
  release-payload-manifest.json \
  RELEASE_IDENTITY.json \
  "Standalone/Project SEAM.app" \
  CLAP/ProjectSEAMEditor.clap \
  VST3/ProjectSEAMEditor.vst3 \
  AU/ProjectSEAMEditor.component \
  Tools/seam_installer_verifier \
  Trust/release-trust-roots.json \
  Ownership/installer-ownership.json \
  release-dependency-closure.json \
  Notices/openssl-LICENSE.txt \
  THIRD_PARTY_NOTICES.md \
  SBOM.spdx.json \
  Documentation/external-beta-documentation.json; do
  [[ -e "$payload/$path" ]] || { echo "missing payload $path" >&2; exit 3; }
done
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
  echo 'payload release version must use strict semantic versioning' >&2
  exit 3
}
if [[ -n "${PROJECT_SEAM_VERSION:-}" && "$PROJECT_SEAM_VERSION" != "$version" ]]; then
  echo 'PROJECT_SEAM_VERSION does not match sealed payload identity' >&2
  exit 3
fi
app_stage="$(mktemp -d)"
plugin_stage="$(mktemp -d)"
work="$(mktemp -d)"
trap 'rm -rf "$app_stage" "$plugin_stage" "$work"' EXIT
mkdir -p \
  "$app_stage/Applications" \
  "$plugin_stage/Library/Audio/Plug-Ins/CLAP" \
  "$plugin_stage/Library/Audio/Plug-Ins/VST3" \
  "$plugin_stage/Library/Audio/Plug-Ins/Components" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Documentation" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Trust" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Ownership" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Notices" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Tools" \
  "$work/scripts"
ditto --norsrc "$payload/Standalone/Project SEAM.app" \
  "$app_stage/Applications/Project SEAM.app"
ditto --norsrc "$payload/CLAP/ProjectSEAMEditor.clap" \
  "$plugin_stage/Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap"
ditto --norsrc "$payload/VST3/ProjectSEAMEditor.vst3" \
  "$plugin_stage/Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3"
ditto --norsrc "$payload/AU/ProjectSEAMEditor.component" \
  "$plugin_stage/Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component"
cp \
  "$payload/RELEASE_IDENTITY.json" \
  "$payload/release-payload-manifest.json" \
  "$payload/release-dependency-closure.json" \
  "$payload/THIRD_PARTY_NOTICES.md" \
  "$payload/SBOM.spdx.json" \
  "$root/scripts/uninstall_macos_plugins.sh" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/"
ditto --norsrc "$payload/Documentation" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Documentation"
ditto --norsrc "$payload/Trust" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Trust"
ditto --norsrc "$payload/Notices" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Notices"
cp "$payload/Ownership/installer-ownership.json" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Ownership/"
cp "$payload/Tools/seam_installer_verifier" \
  "$plugin_stage/Library/Application Support/ProjectSEAM/Tools/"
cp "$root/packaging/macos/scripts/preinstall" "$work/scripts/preinstall"
cp "$payload/Tools/seam_installer_verifier" "$work/scripts/seam_installer_verifier"
chmod 755 "$work/scripts/preinstall" "$work/scripts/seam_installer_verifier"
xattr -cr "$app_stage" "$plugin_stage" "$work/scripts"
pkgbuild --root "$app_stage" --scripts "$work/scripts" \
  --identifier com.project-seam.standalone --version "$version" \
  "$work/ProjectSEAM-standalone.pkg"
pkgbuild --root "$plugin_stage" --identifier com.project-seam.plugins \
  --version "$version" "$work/ProjectSEAM-plugins.pkg"
sed -e "s/@PROJECT_SEAM_VERSION@/$version/g" \
  "$root/packaging/macos/Distribution.xml.in" > "$work/Distribution.xml"
if [[ "$mode" == "--unsigned" ]]; then
  productbuild --distribution "$work/Distribution.xml" \
    --package-path "$work" "$output"
  pkgutil --check-signature "$output" >"$work/unsigned-signature.log" 2>&1 || true
  echo 'MACOS_UNSIGNED_PKG=PASS'
else
  python3 "$root/scripts/verify_production_signing_input.py" --payload "$payload"
  identity="${APPLE_DEVELOPER_ID_INSTALLER:?APPLE_DEVELOPER_ID_INSTALLER is required; release PKG creation fails closed}"
  productbuild --sign "$identity" --distribution "$work/Distribution.xml" \
    --package-path "$work" "$output"
  pkgutil --check-signature "$output"
  echo 'MACOS_SIGNED_PKG=PASS'
fi
