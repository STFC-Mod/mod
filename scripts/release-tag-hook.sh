#!/bin/bash
# Git reference-transaction hook and shared release-version policy.
set -euo pipefail

ReleaseBranch()
{
  local tag=$1 release patch
  if [[ $tag =~ ^v[0-9]+\.[0-9]+\.([0-9]+)\.([0-9]+)$ ]]; then
    release=${BASH_REMATCH[1]}
    patch=${BASH_REMATCH[2]}
    # Inspect decimal digits without shell integer overflow or leading-zero octal interpretation.
    if [[ $patch =~ ^0+$ ]]; then
      if [[ $release =~ [02468]$ ]]; then
        echo main
        return
      fi
      echo "Release tag rejected: odd releases cannot use patch 0; use an alpha/beta tag or a nonzero patch." >&2
      return 1
    fi
    echo dev
  elif [[ $tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+\.(alpha|beta)\.[0-9]+$ ]]; then
    echo dev
  else
    echo "Release tag rejected: expected vMAJOR.MINOR.RELEASE.PATCH or vMAJOR.MINOR.RELEASE.alpha/beta.NUMBER." >&2
    return 1
  fi
}

if [[ ${1:-} == --branch-for-tag ]]; then
  [[ $# == 2 ]] || exit 1
  ReleaseBranch "$2"
  exit
fi
[[ ${1:-} == prepared ]] || exit 0

while read -r old new ref; do
  case "$ref" in refs/tags/v*) ;; *) continue ;; esac
  # Deleting a mistaken tag must remain possible.
  [[ $new =~ ^0+$ ]] && continue
  required_branch=$(ReleaseBranch "${ref#refs/tags/}") || exit 1
  branch=$(git symbolic-ref --quiet --short HEAD) || branch='detached HEAD'
  if [[ $branch != "$required_branch" ]]; then
    echo "Release tag rejected: ${ref#refs/tags/} requires $required_branch (currently $branch)." >&2
    exit 1
  fi
  commit=$(git rev-parse --verify "$new^{commit}") || exit 1
  if ! git merge-base --is-ancestor "$commit" "refs/heads/$branch"; then
    echo "Release tag rejected: its commit is not on $branch. Merge the PR before tagging." >&2
    exit 1
  fi
done
