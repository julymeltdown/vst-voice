#!/usr/bin/env bash
set -euo pipefail
BINARY="${1:?usage: package_macos_clap.sh BINARY OUTPUT.clap}"
OUTPUT="${2:?usage: package_macos_clap.sh BINARY OUTPUT.clap}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
rm -rf "$OUTPUT"
mkdir -p "$OUTPUT/Contents/MacOS" "$OUTPUT/Contents/Resources"
cp "$BINARY" "$OUTPUT/Contents/MacOS/ProjectSEAMEditor"
sed "s/@PROJECT_VERSION@/0.11.0/g" \
  "$ROOT/packaging/macos/ProjectSEAMEditor-Info.plist.in" \
  > "$OUTPUT/Contents/Info.plist"
cp "$ROOT/assets/demo-human-voicebank-public-domain/provenance.json" \
  "$OUTPUT/Contents/Resources/demo-human-voice-provenance.json"
cp "$ROOT/assets/demo-human-voicebank-public-domain/license/PUBLIC_DOMAIN_NOTICE.md" \
  "$OUTPUT/Contents/Resources/PUBLIC_DOMAIN_NOTICE.md"
chmod 755 "$OUTPUT/Contents/MacOS/ProjectSEAMEditor"
/usr/bin/plutil -lint "$OUTPUT/Contents/Info.plist"
