#!/usr/bin/env bash
set -euo pipefail
plugin="${1:?CLAP plugin path is required}"
output="${2:-clap-validator.txt}"
if command -v clap-validator >/dev/null 2>&1; then
  clap-validator validate "$plugin" --only-failed 2>&1 | tee "$output"
  exit "${PIPESTATUS[0]}"
fi
echo 'NOT_RUN: clap-validator is not installed. CI installs official release 0.4.1.' | tee "$output"
exit 3
