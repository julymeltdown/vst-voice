#!/bin/zsh
# Scheduled functional-readiness scan for Project SEAM.
# Started every seven hours by ~/Library/LaunchAgents/com.projectseam.readiness-scan.plist.
# It works in a dedicated detached worktree so the main checkout is never touched.
set -u

export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

main_repo="${SEAM_SCAN_MAIN_REPO:-/Users/lhs/Downloads/project-seam-usable-alpha-u3-master}"
scan_tree="${SEAM_SCAN_WORKTREE:-$HOME/.codex/worktrees/readiness-scan}"
log_dir="$HOME/Library/Logs/ProjectSEAM/readiness-scan"
lock_dir="$log_dir/.lock"
stamp="$(date +%Y%m%dT%H%M%S)"
log_file="$log_dir/scan-$stamp.log"
summary_file="$log_dir/summary-$stamp.md"
time_limit_seconds="${SEAM_SCAN_TIME_LIMIT:-7200}"

mkdir -p "$log_dir"

notify() {
  SEAM_SCAN_MESSAGE="$1" /usr/bin/osascript \
    -e 'display notification (system attribute "SEAM_SCAN_MESSAGE") with title "SEAM readiness scan"' \
    >/dev/null 2>&1 || true
}

if ! mkdir "$lock_dir" 2>/dev/null; then
  echo "$(date) previous scan still running; skipped" >> "$log_dir/skipped.log"
  exit 0
fi
trap 'rmdir "$lock_dir" 2>/dev/null' EXIT

{
  echo "== scan $stamp"
  git -C "$main_repo" fetch origin || { echo "fetch failed"; exit 1; }
  if [[ ! -e "$scan_tree/.git" ]]; then
    git -C "$main_repo" worktree add --detach "$scan_tree" origin/master || exit 1
  fi
  cd "$scan_tree" || exit 1
  if [[ -n "$(git status --porcelain)" ]]; then
    echo "scan worktree is dirty; stopping without changes"
    git status --short
    exit 1
  fi
  git checkout --quiet --detach origin/master || exit 1
  echo "HEAD $(git rev-parse --short HEAD)"
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
