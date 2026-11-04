"""PlatformIO guard: this transition can upload the application only."""
Import("env")
import subprocess
from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS

project = Path(env.subst("$PROJECT_DIR"))
try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"],
        cwd=project, text=True
    ).strip()
except (OSError, subprocess.CalledProcessError):
    version = "unknown-source"
env.Append(CPPDEFINES=[("GIT_VERSION", "ella-transition-" + version)])

# Reject dangerous targets before PlatformIO can touch the installed board.
# No serial environment is provided: serial uploads can include the bootloader
# and partition table. espota uploads only the application image.
for target in COMMAND_LINE_TARGETS:
    if target in {"buildfs", "uploadfs", "erase", "erase_flash", "uploadfsota"}:
        raise RuntimeError("This transitional project allows application firmware only; filesystem/erase targets are disabled")
if env.GetProjectOption("upload_protocol") != "espota":
    raise RuntimeError("The transitional upload protocol must be espota")

