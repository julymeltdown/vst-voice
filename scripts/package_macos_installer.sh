#!/usr/bin/env bash
set -euo pipefail
bundle="${1:?notarized .clap bundle is required}"
output="${2:?output PKG is required}"
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(python3 "$root/tools/phase13a/release_identity.py" \
  --source-root "$root" --field version)"
case "$(uname -s)" in Darwin) ;; *) echo 'pkgbuild must run on macOS' >&2; exit 2;; esac
info_plist="$bundle/Contents/Info.plist"
[[ -f "$info_plist" ]] || { echo 'CLAP bundle Info.plist is required' >&2; exit 3; }
bundle_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$info_plist")"
[[ "$bundle_version" == "$version" ]] || {
  echo 'CLAP bundle version does not match the canonical source identity' >&2
  exit 3
}
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/Library/Audio/Plug-Ins/CLAP"
cp -R "$bundle" "$stage/Library/Audio/Plug-Ins/CLAP/"
pkgbuild --root "$stage" --identifier com.project-seam.editor.clap.pkg --version "$version" "$output"
