#!/usr/bin/env bash
# Build the three Two to Tango images into out/:
#   loader.bin  resident loader, installed at 0x0800C000 through the TBS bootloader
#   edgetx.bin  EdgeTX, at 0x08020000
#   elrs.bin    ExpressLRS TX image, at 0x080E8000 (no bind phrase unless --phrase)
#
#   ./build.sh [--phrase "bind phrase"] [--clean]
#
# Needs PlatformIO (pio), CMake, Ninja and Python 3 with requirements.txt. The
# loader build installs PlatformIO's arm-none-eabi-gcc and CMSIS headers, and
# the other two builds use those; set ARM_TOOLCHAIN_DIR to use another gcc.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

PHRASE=()
CLEAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --phrase) PHRASE=(--phrase "$2"); shift 2 ;;
    --clean) CLEAN=1; shift ;;
    *) echo "usage: $0 [--phrase \"bind phrase\"] [--clean]" >&2; exit 2 ;;
  esac
done

PIO="${PIO:-pio}"
PYTHON="${PYTHON:-$(command -v python3 || command -v python)}"
# CMake on Windows wants C:/ paths, not Git Bash's /c/
native() { if command -v cygpath >/dev/null; then cygpath -m "$1"; else echo "$1"; fi; }

if [ ! -f edgetx/CMakeLists.txt ] || [ ! -f elrs/ExpressLRS/src/platformio.ini ]; then
  git submodule update --init --recursive
fi
if [ "$CLEAN" = 1 ]; then
  rm -rf build out loader/.pio elrs/build
fi
mkdir -p out build

echo "== loader"
(cd loader && "$PIO" run -s)
cp loader/.pio/build/loader/firmware.bin out/loader.bin

echo "== ExpressLRS image"
"$PYTHON" elrs/build.py ${PHRASE[@]+"${PHRASE[@]}"}
cp elrs/build/tango2-elrs.bin out/elrs.bin

echo "== EdgeTX"
ARM_BIN="${ARM_TOOLCHAIN_DIR:-$HOME/.platformio/packages/toolchain-gccarmnoneeabi/bin}"
B=build/edgetx
if [ ! -f "$B/build.ninja" ]; then
  # EdgeTX asks for Arm GNU 14.2; this target is built and tested with
  # PlatformIO's 9.2.1, hence USE_UNSUPPORTED_TOOLCHAIN.
  cmake -S edgetx -B "$B" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$(native "$ROOT/edgetx/cmake/toolchain/arm-none-eabi.cmake")" \
    -DARM_TOOLCHAIN_DIR="$(native "$ARM_BIN")" -DUSE_UNSUPPORTED_TOOLCHAIN=ON \
    -DEdgeTX_SUPERBUILD=0 -DNATIVE_BUILD=0 \
    -DPCB=TANGO2 -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$(native "$PYTHON")"
fi
ninja -C "$B" firmware
"$PYTHON" tools/check_edgetx.py "$B/firmware.bin" "$B/firmware.elf"
cp "$B/firmware.bin" out/edgetx.bin

echo "== out/"
(cd out && sha256sum loader.bin edgetx.bin elrs.bin)
