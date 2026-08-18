#!/usr/bin/env bash
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 2; }
rm -rf \
  /Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap \
  /Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3 \
  /Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component \
  "/Library/Application Support/ProjectSEAM"
pkgutil --forget com.project-seam.plugins >/dev/null 2>&1 || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo 'MACOS_UNINSTALL=PASS'
