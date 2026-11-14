# Create a release

This checkout has a `reference-transaction` hook installed from `scripts/release-tag-hook.sh`. It rejects creation
or movement of `v*` tags unless the checked-out branch matches the version policy and contains the target commit. Deleting a
mistaken tag is still allowed. This also covers GUI tools that use Git and honor its hooks.

For a new clone, install it with the following command, after checking for an existing hook to preserve:

```sh
cp scripts/release-tag-hook.sh "$(git rev-parse --git-path hooks)/reference-transaction"
chmod +x "$(git rev-parse --git-path hooks)/reference-transaction"
```

Merge the release PR into `main` or `dev` first. Check out that branch, update it from `origin`, and wait for its
push-triggered Build workflow to succeed. Then create the tag:

```sh
bash scripts/create-release-tag.sh v1.1.10.0
git push origin refs/tags/v1.1.10.0
```

The script refuses feature branches, detached checkouts, tracked changes, and a local branch that differs from its
published tip. It creates an annotated tag locally and does not push it.

| Version | Required branch |
| --- | --- |
| `vMAJOR.MINOR.RELEASE.0`, with an even RELEASE (e.g. `v1.1.10.0`) | `main` |
| Numeric versions with a nonzero PATCH (e.g. `v1.1.10.1`, `v1.1.11.1`) | `dev` |
| Alpha or beta versions (e.g. `v10.1.11.beta.1`) | `dev` |
| Odd RELEASE with PATCH 0 (e.g. `v1.1.11.0`), or any other format | Rejected |

The third number determines parity, independently of major and minor. The hook, creation script, and workflow use
the same version policy. The workflow checks that the tagged commit is reachable from the required remote branch
and has a successful push-triggered Build on that branch; a successful PR build is insufficient.

Run `python3 tests/release_tags.py` to check the version policy and Git hook in an isolated repository.

Tags identify commits, not branches. Direct `git tag` bypasses the creation script but is checked by the installed
hook. GitHub tag creation bypasses local hooks. The workflow
blocks release publication after an invalid tag is pushed; it does not prevent or delete the tag. Tags pointing to
older workflow versions also use those older checks.

For server-side control, configure a GitHub tag ruleset for `v*` with **Restrict creations**, allowing only designated
release maintainers or automation to bypass it. Those maintainers must use the checked creation path: the ruleset
restricts who can create a tag, rather than which branch contains its commit.
