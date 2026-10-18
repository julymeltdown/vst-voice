#!/bin/zsh
# Scheduled functional-readiness scan for Project SEAM.
# bootstrap.sh has already fast-forwarded this clone to origin/master and checked that it is clean.
# This script runs one unattended Codex scan with a hard time limit and reports the result.
set -u

export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

scan_tree="$(cd "$(dirname "$0")/../.." && pwd)"
log_dir="$HOME/Library/Logs/ProjectSEAM/readiness-scan"
stamp="${SEAM_SCAN_STAMP:-$(date +%Y%m%dT%H%M%S)}"
log_file="$log_dir/scan-$stamp.log"
summary_file="$log_dir/summary-$stamp.md"
time_limit_seconds="${SEAM_SCAN_TIME_LIMIT:-7200}"

mkdir -p "$log_dir"

notify() {
  SEAM_SCAN_MESSAGE="$1" /usr/bin/osascript \
    -e 'display notification (system attribute "SEAM_SCAN_MESSAGE") with title "SEAM readiness scan"' \
    >/dev/null 2>&1 || true
}

{
  echo "== scan $stamp in $scan_tree at $(git -C "$scan_tree" rev-parse --short HEAD)"
  gtimeout --kill-after=60 "$time_limit_seconds" \
    codex exec -C "$scan_tree" -s danger-full-access -c 'approval_policy="never"' \
      -o "$summary_file" - < "$scan_tree/tools/readiness_scan/PROMPT.md"
  echo "codex exit $?"
} >> "$log_file" 2>&1

if [[ -s "$summary_file" ]]; then
  notify "$(head -n 1 "$summary_file")"
else
  notify "Scan $stamp produced no summary; see $log_file"
fi
