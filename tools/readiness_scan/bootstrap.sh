#!/bin/zsh
# Launch-agent entry point for the Project SEAM readiness scan.
# Install a copy at ~/.codex/readiness-scan/bootstrap.sh; the agent runs that copy, so updating the
# clone never rewrites the script that is executing. The clone lives outside ~/Downloads because
# macOS privacy protection blocks launch agents from reading that folder.
set -u

export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

scan_tree="${SEAM_SCAN_CLONE:-$HOME/.codex/worktrees/readiness-scan}"
log_dir="$HOME/Library/Logs/ProjectSEAM/readiness-scan"
lock_dir="$log_dir/.lock"
stamp="$(date +%Y%m%dT%H%M%S)"
log_file="$log_dir/scan-$stamp.log"

mkdir -p "$log_dir"
if ! mkdir "$lock_dir" 2>/dev/null; then
  echo "$(date) previous scan still running; skipped" >> "$log_dir/skipped.log"
  exit 0
fi
trap 'rmdir "$lock_dir" 2>/dev/null' EXIT

{
  echo "== bootstrap $stamp"
  if [[ ! -d "$scan_tree/.git" ]]; then
    echo "scan clone missing at $scan_tree; create it as described in docs/plans/FUNCTIONAL_READINESS.md section 9"
    exit 1
  fi
  cd "$scan_tree" || exit 1
  git fetch origin || { echo "fetch failed"; exit 1; }
  if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "scan clone has tracked changes; stopping"
    git status --short --untracked-files=no
    exit 1
  fi
  git checkout --quiet --detach origin/master || exit 1
  echo "HEAD $(git rev-parse --short HEAD)"
} >> "$log_file" 2>&1 || exit 1

SEAM_SCAN_STAMP="$stamp" /bin/zsh "$scan_tree/tools/readiness_scan/run_scan.sh"
