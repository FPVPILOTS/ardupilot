#!/usr/bin/env bash
# Preview upstream ArduPilot changes WITHOUT modifying the working branch.
# Usage: bash Tools/scripts/spektrum_upstream_preview.sh [upstream/master|<release-tag>]
set -euo pipefail
root="$(git rev-parse --show-toplevel)"
cd "$root"
printf 'Local ArduPilot: %s\n' "$root"
if [ -n "$(git status --porcelain)" ]; then
    printf 'ERROR: worktree is not clean. Commit or stash your changes first.\n' >&2
    exit 2
fi
if ! git remote get-url upstream >/dev/null 2>&1; then
    git remote add upstream https://github.com/ArduPilot/ardupilot.git
fi
# Tags allow the user to target an official stable release as well as master.
git fetch --prune --tags upstream master
target="${1:-upstream/master}"
git rev-parse --verify "${target}^{commit}" >/dev/null
branch="$(git branch --show-current)"
base="$(git merge-base HEAD "$target")"
printf '\nCurrent branch: %s\nCurrent commit: %s\nUpstream target: %s\n' \
  "$branch" "$(git rev-parse --short HEAD)" "$target"
printf 'Merge-base: %s\n\n' "$(git rev-parse --short "$base")"
printf 'New upstream commits (first 30):\n'
git log --oneline --no-merges "HEAD..$target" | sed -n '1,30p'
printf '\nUpstream changed files since merge-base (first 100):\n'
git diff --name-only "$base" "$target" | sed -n '1,100p'
printf '\nCustom branch changed files since merge-base (first 100):\n'
git diff --name-only "$base" HEAD | sed -n '1,100p'
printf '\nFiles touched on BOTH sides (likely conflict candidates):\n'
comm -12 \
    <(git diff --name-only "$base" "$target" | sort) \
    <(git diff --name-only "$base" HEAD | sort) || true
printf '\nPREVIEW ONLY: no checkout, rebase, merge, flash or push was performed.\n'
printf 'Next: make a fresh upgrade test branch/worktree, resolve overlaps,\n'
printf 'run host tests + Pixhawk6C builds, review APJ/IOMCU SHA, then bench check.\n'
