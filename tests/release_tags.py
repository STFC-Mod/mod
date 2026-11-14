"""Exercise release version rules and the installed hook without changing this checkout."""

from pathlib import Path
import shutil
import subprocess
import tempfile


HOOK = Path(__file__).resolve().parents[1] / "scripts/release-tag-hook.sh"
for tag, expected in {
    "v1.1.10.0": "main",
    "v10.1.12.0": "main",
    "v1.1.10.1": "dev",
    "v1.1.11.1": "dev",
    "v10.1.11.beta.1": "dev",
    "v1.1.10.alpha.0": "dev",
    "v1.1.11.0": None,
    "v1.1.11.00": None,
    "v1.1.10": None,
    "v1.1.10.rc.1": None,
    "v1.1.10.0-extra": None,
}.items():
    result = subprocess.run(
        ["bash", str(HOOK), "--branch-for-tag", tag], capture_output=True, text=True
    )
    assert (result.returncode == 0) == (expected is not None), (tag, result.stderr)
    if expected:
        assert result.stdout.strip() == expected, tag

with tempfile.TemporaryDirectory(prefix="stfc-release-tags-") as directory:
    def git(*args, ok=True):
        result = subprocess.run(
            ["git", "-C", directory, *args], capture_output=True, text=True
        )
        assert (result.returncode == 0) == ok, (args, result.stderr)
        return result.stdout.strip()

    git("init", "-b", "main")
    git("config", "user.name", "Release tests")
    git("config", "user.email", "test@example.invalid")
    git("commit", "--allow-empty", "-m", "Base")
    installed = Path(directory) / ".git/hooks/reference-transaction"
    shutil.copyfile(HOOK, installed)
    installed.chmod(0o755)
    git("tag", "v1.1.10.0")
    git("tag", "-a", "v1.1.12.0", "-m", "Stable")
    git("tag", "v1.1.10.1", ok=False)
    git("tag", "v1.1.11.0", ok=False)
    git("tag", "v10.1.11.beta.1", ok=False)
    git("switch", "-c", "dev")
    git("tag", "-a", "v10.1.11.beta.1", "-m", "Beta")
    git("tag", "v1.1.10.1")
    git("tag", "v1.1.14.0", ok=False)
    git("tag", "v1.1.11.0", ok=False)
    git("switch", "-c", "feature/test")
    git("commit", "--allow-empty", "-m", "PR only")
    feature = git("rev-parse", "HEAD")
    git("tag", "v1.1.16.0", ok=False)
    git("tag", "v1.1.11.beta.2", ok=False)
    git("tag", "-d", "v1.1.10.0")
    git("tag", "internal-test")
    git("switch", "main")
    git("tag", "v1.1.16.0", feature, ok=False)
    git("switch", "--detach")
    git("tag", "v1.1.16.0", ok=False)

print("Release policy and Git hook checks passed")
