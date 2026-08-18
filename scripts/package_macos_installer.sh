#!/usr/bin/env bash
set -euo pipefail
bundle="${1:?notarized .clap bundle is required}"
output="${2:?output PKG is required}"
case "$(uname -s)" in Darwin) ;; *) echo 'pkgbuild must run on macOS' >&2; exit 2;; esac
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/Library/Audio/Plug-Ins/CLAP"
cp -R "$bundle" "$stage/Library/Audio/Plug-Ins/CLAP/"
pkgbuild --root "$stage" --identifier com.project-seam.editor.clap.pkg --version 0.11.0 "$output"
