#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
data_root="${SEAM_INSTALL_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/ProjectSEAM}"
clap_root="${SEAM_CLAP_ROOT:-$HOME/.clap}"
mkdir -p "$data_root" "$clap_root"
[[ -f "$root/CLAP/ProjectSEAMEditor.clap" ]] || { echo 'missing CLAP module' >&2; exit 2; }
[[ -d "$root/CLAP/ProjectSEAMEditor.resources" ]] || { echo 'missing CLAP resources' >&2; exit 3; }
rm -f "$clap_root/ProjectSEAMEditor.clap"
rm -rf "$clap_root/ProjectSEAMEditor.resources"
cp "$root/CLAP/ProjectSEAMEditor.clap" "$clap_root/ProjectSEAMEditor.clap"
cp -R "$root/CLAP/ProjectSEAMEditor.resources" "$clap_root/ProjectSEAMEditor.resources"
cp "$root/release-manifest.json" "$data_root/release-manifest.json"
printf '%s\n' \
  "$clap_root/ProjectSEAMEditor.clap" \
  "$clap_root/ProjectSEAMEditor.resources" \
  > "$data_root/installed-files.txt"
echo "Installed unsigned development CLAP to $clap_root"
