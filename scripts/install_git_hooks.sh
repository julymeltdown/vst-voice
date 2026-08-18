#!/bin/sh
set -eu
root=$(git rev-parse --show-toplevel)
git -C "$root" config core.hooksPath .githooks
printf 'Configured core.hooksPath=.githooks for %s\n' "$root"
