# PlatformIO pre-build script: turns the VERSION file into FW_VERSION, and the GitHub build
# number into FW_BUILD (0 for local builds). Over-the-air updates compare build numbers.
Import("env")  # noqa: F821  (provided by PlatformIO)
import os
from pathlib import Path

version = Path(env.subst("$PROJECT_DIR"), "VERSION").read_text().strip()
build = int(os.environ.get("GITHUB_RUN_NUMBER", "0") or 0)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version)), ("FW_BUILD", build)])
print(f"Grid Mix firmware version {version}, build {build}")
