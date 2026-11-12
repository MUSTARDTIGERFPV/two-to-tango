"""Build the ExpressLRS TX image for the TBS Tango 2 (flash 0x080E8000, abi/elrs_image.h).

  python build.py [--phrase "bind phrase" | --uid 1849bb1932aa] [--domain N] [--debug-log] [--clean] [-j N]

Output: build/tango2-elrs.elf, build/tango2-elrs.bin (image_size + image_crc32
filled in by abi/finish_image.py), build/tango2-elrs.map.
"""
import argparse
import concurrent.futures
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ELRS = os.path.join(HERE, "ExpressLRS", "src")
PLAT = os.path.join(HERE, "platform")
BUILD = os.path.join(HERE, "build")
PKG = os.path.expanduser("~/.platformio/packages")
GCC = os.path.join(PKG, "toolchain-gccarmnoneeabi", "bin", "arm-none-eabi-")

# ExpressLRS sources (the tango2 branch of the ExpressLRS submodule).
ELRS_SRCS = [
    "src/tx_main.cpp", "src/common.cpp", "src/rxtx_common.cpp", "src/dynpower.cpp",
    "lib/OTA/OTA.cpp", "lib/FHSS/FHSS.cpp", "lib/FHSS/random.cpp", "lib/CRC/crc.cpp",
    "lib/CONFIG/config.cpp", "lib/DEVICE/device.cpp", "lib/POWERMGNT/POWERMGNT.cpp",
    "lib/SX127xDriver/SX127x.cpp", "lib/SX127xDriver/SX127xHal.cpp",
    "lib/StubbornSender/stubborn_sender.cpp", "lib/StubbornReceiver/stubborn_receiver.cpp",
    "lib/MSP/msp.cpp", "lib/logging/logging.cpp",
    "lib/CrsfProtocol/CRSFConnector.cpp", "lib/CrsfProtocol/CRSFEndpoint.cpp",
    "lib/CrsfProtocol/CRSFParser.cpp", "lib/CrsfProtocol/CRSFRouter.cpp",
    "lib/CrsfProtocol/RxTxEndpoint.cpp",
    "lib/tx-crsf/TXModuleEndpoint.cpp", "lib/tx-crsf/TXModuleParameters.cpp",
    "lib/tx-crsf/TXOTAConnector.cpp", "lib/tx-crsf/TXUSBConnector.cpp", "lib/tx-crsf/devTXLUA.cpp",
    "lib/VTX/devVTX.cpp",
]
# Tango 2 platform layer (replaces Arduino core, ESP HALs, UART handset, options/hardware JSON).
PLAT_SRCS = [
    "image_main.cpp", "tango2_platform.cpp", "hwTimer_tango2.cpp", "RFAMP_hal.cpp",
    "elrs_eeprom_tango2.cpp", "options_tango2.cpp", "HostHandset.cpp", "stubs.cpp", "libc_glue.c", "image_header.c",
]
# Library dirs that must NOT be on the include path (replaced by platform/).
LIB_EXCLUDE = {"SPIEx"}

CPU = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]


def git(*args, cwd):
    try:
        return subprocess.check_output(["git"] + list(args), cwd=cwd, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""


def uid_from_phrase(phrase):
    # Same as ExpressLRS python/build_flags.py: md5 of the literal define text
    return hashlib.md5(('-DMY_BINDING_PHRASE="%s"' % phrase).encode()).digest()[:6]


def includes():
    inc = [PLAT, os.path.join(PLAT, "target"), os.path.join(ROOT, "abi"),
           os.path.join(ELRS, "include"), os.path.join(ELRS, "src")]
    libdir = os.path.join(ELRS, "lib")
    for d in sorted(os.listdir(libdir)):
        if d not in LIB_EXCLUDE and os.path.isdir(os.path.join(libdir, d)):
            inc.append(os.path.join(libdir, d))
    inc += [os.path.join(PKG, "framework-cmsis", "CMSIS", "Core", "Include"),
            os.path.join(PKG, "framework-arduinoststm32", "system", "Drivers", "CMSIS", "Device", "ST", "STM32F4xx", "Include")]
    return inc


def defines(args):
    ver = git("describe", "--tags", "--always", cwd=ELRS) or "unknown"
    sha = git("rev-parse", "--short=6", "HEAD", cwd=ELRS) or "000000"
    d = ["TARGET_TX=1", "RADIO_SX127X=1", "RADIO_SX1272=1", "Regulatory_Domain_FCC_915=1",
         "STM32F407xx", "MAVLINK_NO_SIGN_PACKET", "MAVLINK_NO_SIGNATURE_CHECK",
         "TANGO2_DOMAIN=%d" % args.domain,
         "LATEST_VERSION=%s" % ",".join("'%s'" % c for c in ("%s tango2" % ver)),
         "LATEST_COMMIT=%s" % ",".join("0x%s" % c for c in sha[:6])]
    uid = bytes.fromhex(args.uid) if getattr(args, "uid", None) else (
        uid_from_phrase(args.phrase) if args.phrase is not None else None)
    if uid:
        d.append("MY_UID=%s" % ",".join("0x%02x" % b for b in uid))
    if args.debug_log:
        d.append("DEBUG_LOG")
    return ["-D" + x for x in d], ver


def compile_one(src, obj, flags):
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    is_c = src.endswith(".c")
    cmd = [GCC + ("gcc" if is_c else "g++")] + flags["c" if is_c else "cxx"] + ["-MMD", "-c", src, "-o", obj]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return src, r.returncode, r.stdout + r.stderr


def up_to_date(src, obj, flags_sig):
    dep = obj[:-2] + ".d"
    sig = obj[:-2] + ".sig"
    if not (os.path.exists(obj) and os.path.exists(dep) and os.path.exists(sig)):
        return False
    if open(sig).read() != flags_sig:
        return False
    t = os.path.getmtime(obj)
    text = open(dep).read().replace("\\\n", " ")
    for f in text.split(":", 1)[1].split():
        f = f.replace("\\ ", " ")
        if os.path.exists(f) and os.path.getmtime(f) > t:
            return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phrase", default=None, help="bind phrase (default: none; the flasher or the ELRS Lua sets it)")
    ap.add_argument("--uid", help="UID as 12 hex digits (instead of --phrase), e.g. 1849bb1932aa")
    ap.add_argument("--domain", type=int, default=1, help="FHSS domain index (1 = FCC915, 2 = EU868)")
    ap.add_argument("--debug-log", action="store_true")
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    defs, ver = defines(args)
    common = CPU + ["-Os", "-g", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wno-unused-function",
                    "-include", "Tango2_TX.h"] + defs + ["-I" + i for i in includes()]
    flags = {
        "c": common + ["-std=gnu11"],
        "cxx": common + ["-std=gnu++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                         "-fno-use-cxa-atexit", "-Wno-register", "-Wno-class-memaccess"],
    }
    sig = repr(flags)
    if args.clean and os.path.isdir(BUILD):
        import shutil
        shutil.rmtree(BUILD)
    jobs = []
    for s in ELRS_SRCS:
        jobs.append((os.path.join(ELRS, s), os.path.join(BUILD, "obj", "elrs", s.replace("/", "_")[:-4] + ".o")))
    for s in PLAT_SRCS:
        base = s.rsplit(".", 1)[0]
        jobs.append((os.path.join(PLAT, s), os.path.join(BUILD, "obj", "plat", base + ".o")))
    todo = [(s, o) for s, o in jobs if not up_to_date(s, o, sig)]
    failed = False
    with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
        for src, rc, out in ex.map(lambda so: compile_one(so[0], so[1], flags), todo):
            if out.strip():
                print(out.rstrip())
            if rc:
                failed = True
                print("FAILED:", os.path.relpath(src, HERE))
            else:
                open(dict(todo)[src][:-2] + ".sig", "w").write(sig)
    if failed:
        sys.exit(1)

    elf = os.path.join(BUILD, "tango2-elrs.elf")
    binf = os.path.join(BUILD, "tango2-elrs.bin")
    ld = [GCC + "g++"] + CPU + ["-nostartfiles", "--specs=nano.specs", "-T", os.path.join(HERE, "image.ld"),
                                "-Wl,--gc-sections", "-Wl,-Map," + os.path.join(BUILD, "tango2-elrs.map"),
                                "-Wl,--print-memory-usage"] + [o for _, o in jobs] + ["-lstdc++", "-lsupc++", "-lm", "-lc", "-lgcc", "-o", elf]
    subprocess.check_call(ld)
    subprocess.check_call([GCC + "objcopy", "-O", "binary", "-j", ".header", "-j", ".text", "-j", ".data", elf, binf])
    subprocess.check_call([sys.executable, os.path.join(ROOT, "abi", "finish_image.py"), binf])
    subprocess.check_call([GCC + "size", "-A", elf])
    print("version:", ver)


if __name__ == "__main__":
    main()
