# PlatformIO pre-build script: stamps FW_VERSION from `git describe` (e.g. v0.1.0,
# v0.1.0-3-gabc1234-dirty). Override with the FW_VERSION environment variable.
import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)


def git_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return "dev"


version = (os.environ.get("FW_VERSION") or git_version()).lstrip("v")
env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % version)])  # noqa: F821
print("FW_VERSION =", version)
