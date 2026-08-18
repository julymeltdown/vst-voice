#!/usr/bin/env bash
set -euo pipefail
PLUGIN="${1:?plugin path required}"
OUT="${2:-clap-validator-phase12c.txt}"
VERSION="0.4.1"
if command -v clap-validator >/dev/null 2>&1; then
  clap-validator validate "$PLUGIN" --only-failed | tee "$OUT"
  exit ${PIPESTATUS[0]}
fi
printf 'NOT_RUN: clap-validator %s is not installed on this runner.\n' "$VERSION" | tee "$OUT"
exit 3
