# PlatformIO pre-build script: turns the VERSION file into the FW_VERSION macro.
Import("env")  # noqa: F821  (provided by PlatformIO)
from pathlib import Path

version = Path(env.subst("$PROJECT_DIR"), "VERSION").read_text().strip()
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
print(f"Grid Mix firmware version {version}")
