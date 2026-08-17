#!/usr/bin/env bash
set -euo pipefail
binary="${1:?Mach-O CLAP binary is required}"
bundle="${2:?output .clap bundle is required}"
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)" in Darwin) ;; *) echo 'macOS packaging must run on macOS' >&2; exit 2;; esac
file "$binary" | grep -q 'Mach-O' || { echo 'input is not a Mach-O binary' >&2; exit 3; }
rm -rf "$bundle"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$binary" "$bundle/Contents/MacOS/ProjectSEAMEditor"
cp "$root/packaging/macos/ProjectSEAMEditor-Info.plist" "$bundle/Contents/Info.plist"
cp "$root/assets/demo-human-voicebank-public-domain/provenance.json" "$bundle/Contents/Resources/demo-voice-provenance.json"
cp "$root/assets/demo-human-voicebank-public-domain/license/PUBLIC_DOMAIN_NOTICE.md" "$bundle/Contents/Resources/PUBLIC_DOMAIN_NOTICE.md"
chmod 755 "$bundle/Contents/MacOS/ProjectSEAMEditor"
plutil -lint "$bundle/Contents/Info.plist"
