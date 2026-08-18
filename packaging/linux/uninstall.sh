#!/usr/bin/env bash
set -euo pipefail
data_root="${SEAM_INSTALL_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/ProjectSEAM}"
clap_root="${SEAM_CLAP_ROOT:-$HOME/.clap}"
rm -f "$clap_root/ProjectSEAMEditor.clap"
rm -rf "$clap_root/ProjectSEAMEditor.resources"
rm -rf "$data_root"
echo 'Project SEAM development package removed'
