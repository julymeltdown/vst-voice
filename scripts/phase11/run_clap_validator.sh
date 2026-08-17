#!/usr/bin/env bash
set -euo pipefail
PLUGIN="${1:?usage: run_clap_validator.sh PLUGIN [OUTPUT]}"
OUTPUT="${2:-phase11-clap-validator.txt}"
if ! command -v clap-validator >/dev/null 2>&1; then
  printf '%s\n' 'NOT_RUN: clap-validator is not installed in this execution environment.' | tee "$OUTPUT"
  exit 3
fi
set +e
clap-validator validate "$PLUGIN" --only-failed 2>&1 | tee "$OUTPUT"
STATUS=${PIPESTATUS[0]}
set -e
exit "$STATUS"
