"""Print the address of an EdgeTX symbol (or symbol+offset) in the current build.
  python etx_sym.py storageDirtyMsk g_eeGeneral+100
Always resolve addresses from the ELF you flashed: they move on every rebuild.
ELF defaults to build/edgetx/firmware.elf; nm comes from $ARM_TOOLCHAIN_DIR or
PlatformIO's toolchain."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLCHAIN = os.environ.get("ARM_TOOLCHAIN_DIR") or os.path.expanduser(
    "~/.platformio/packages/toolchain-gccarmnoneeabi/bin")
NM = os.path.join(TOOLCHAIN, "arm-none-eabi-nm")
ELF = os.environ.get("EDGETX_ELF") or os.path.join(ROOT, "build", "edgetx", "firmware.elf")
syms = {}
for line in subprocess.run([NM, "-C", ELF], capture_output=True, text=True, check=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3:
        syms.setdefault(p[2].split("(")[0], int(p[0], 16))
out = []
for a in sys.argv[1:]:
    name, _, off = a.partition("+")
    out.append("0x%08x" % (syms[name] + (int(off, 0) if off else 0)))
print(" ".join(out))
