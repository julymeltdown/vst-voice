#!/usr/bin/env bash
set -euo pipefail
BUNDLE="${1:?usage: package_macos_installer.sh BUNDLE OUTPUT.pkg}"
OUTPUT="${2:?usage: package_macos_installer.sh BUNDLE OUTPUT.pkg}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Library/Audio/Plug-Ins/CLAP"
cp -R "$BUNDLE" "$STAGE/Library/Audio/Plug-Ins/CLAP/"
pkgbuild --root "$STAGE" \
  --identifier com.project-seam.editor.clap.pkg \
  --version 0.11.0 "$OUTPUT"
if [[ -n "${APPLE_DEVELOPER_ID_INSTALLER:-}" ]]; then
  productsign --sign "$APPLE_DEVELOPER_ID_INSTALLER" "$OUTPUT" "${OUTPUT%.pkg}.signed.pkg"
fi
