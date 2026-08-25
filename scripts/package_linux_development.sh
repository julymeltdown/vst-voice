#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
clap="${1:?CLAP binary required}"
out="${2:?output .tar.gz required}"
[[ -f "$clap" && -s "$clap" ]] || { echo 'CLAP binary must be a non-empty file' >&2; exit 2; }
resources="$(dirname "$clap")/ProjectSEAMEditor.resources"
[[ -d "$resources" ]] || { echo "missing resource sidecar: $resources" >&2; exit 3; }
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/ProjectSEAM/CLAP"
cp "$clap" "$stage/ProjectSEAM/CLAP/ProjectSEAMEditor.clap"
cp -R "$resources" "$stage/ProjectSEAM/CLAP/ProjectSEAMEditor.resources"
cp "$root/THIRD_PARTY_NOTICES.md" "$root/SBOM.spdx.json" "$stage/ProjectSEAM/"
cp "$root/packaging/linux/install.sh" "$root/packaging/linux/uninstall.sh" "$stage/ProjectSEAM/"
printf '%s\n' 'UNSIGNED DEVELOPMENT BUILD — not validator-certified or release eligible.' > "$stage/ProjectSEAM/UNSIGNED-DEVELOPMENT-BUILD.txt"
python3 "$root/scripts/write_release_manifest.py" --payload "$stage/ProjectSEAM" --output "$stage/ProjectSEAM/release-manifest.json" --status DEVELOPMENT_CLAP_ONLY --version "${SEAM_VERSION:-0.13.1-local}"
GZIP=-n tar --sort=name --mtime='UTC 1980-01-01' --owner=0 --group=0 --numeric-owner -C "$stage" -czf "$out" ProjectSEAM
echo "LINUX_DEVELOPMENT_PACKAGE=$out"
