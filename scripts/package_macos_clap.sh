#!/usr/bin/env bash
set -euo pipefail
binary="${1:?Mach-O CLAP binary is required}"
bundle="${2:?output .clap bundle is required}"
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(python3 "$root/tools/phase13a/release_identity.py" \
  --source-root "$root" --field version)"
case "$(uname -s)" in Darwin) ;; *) echo 'macOS packaging must run on macOS' >&2; exit 2;; esac
[[ "$bundle" == *.clap ]] || { echo 'output must use the .clap extension' >&2; exit 3; }
[[ ! -e "$bundle" && ! -L "$bundle" ]] || {
  echo 'output .clap bundle already exists' >&2
  exit 3
}
file "$binary" | grep -q 'Mach-O' || { echo 'input is not a Mach-O binary' >&2; exit 3; }
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$binary" "$bundle/Contents/MacOS/ProjectSEAMEditor"
sed -e "s/@PROJECT_VERSION@/$version/g" \
  "$root/packaging/macos/ProjectSEAMEditor-Info.plist.in" \
  > "$bundle/Contents/Info.plist"
cp "$root/packaging/release-resource-inventory.json" \
  "$bundle/Contents/Resources/release-resource-inventory.json"
chmod 755 "$bundle/Contents/MacOS/ProjectSEAMEditor"
plutil -lint "$bundle/Contents/Info.plist"
