"""Print shell exports for the compiler and CMSIS headers PlatformIO used for
the loader, so the ExpressLRS image and EdgeTX build with the same ones.
usage: pio project metadata -d loader -e loader --json-output | pio_paths.py"""
import json
import os
import shlex
import sys

env = json.load(sys.stdin)["loader"]
paths = {"ARM_TOOLCHAIN_DIR": os.path.dirname(env["cc_path"])}
for inc in (p for group in env["includes"].values() for p in group):
    norm = inc.replace("\\", "/")
    if norm.endswith("CMSIS/Core/Include"):
        paths.setdefault("CMSIS_CORE_INCLUDE", inc)
    elif norm.endswith("Device/ST/STM32F4xx/Include"):
        paths.setdefault("CMSIS_F4_INCLUDE", inc)
missing = {"CMSIS_CORE_INCLUDE", "CMSIS_F4_INCLUDE"} - paths.keys()
if missing:
    sys.exit("PlatformIO metadata has no %s" % ", ".join(sorted(missing)))
for k, v in paths.items():
    print("export %s=%s" % (k, shlex.quote(v.replace("\\", "/"))))
