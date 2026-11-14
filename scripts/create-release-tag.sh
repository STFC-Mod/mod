#!/bin/bash
# Create a release tag only at the published main or dev tip. Push it explicitly after reviewing it.
set -euo pipefail

if [[ $# != 1 || "$1" != v* ]]; then
  echo "Usage: $0 v<version>" >&2
  exit 1
fi
tag=$1
git check-ref-format "refs/tags/$tag"
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
required_branch=$(bash "$script_dir/release-tag-hook.sh" --branch-for-tag "$tag")
branch=$(git symbolic-ref --quiet --short HEAD) || {
  echo "Check out main or dev before creating a release tag." >&2
  exit 1
}
if [[ $branch != "$required_branch" ]]; then
  echo "$tag requires $required_branch; current branch: $branch" >&2
  exit 1
fi
if [[ -n $(git status --porcelain --untracked-files=no) ]]; then
  echo "Commit or stash tracked changes before creating a release tag." >&2
  exit 1
fi
git fetch --no-tags origin "+refs/heads/$branch:refs/remotes/origin/$branch"
if [[ $(git rev-parse HEAD) != $(git rev-parse "refs/remotes/origin/$branch") ]]; then
  echo "Local $branch must match origin/$branch. Publish or update the branch before tagging." >&2
  exit 1
fi
git tag -a "$tag" -m "Release $tag" HEAD
printf 'Created %s on %s. To publish: git push origin refs/tags/%s\n' "$tag" "$branch" "$tag"
