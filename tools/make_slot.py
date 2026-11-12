"""Build the 256 KB radio-image slot for flash sectors 10-11 (0x080C0000):
the TBS Crossfire image at 0x080C0000 and the ELRS image at 0x080E8000
(abi/elrs_image.h). Write it with `tango2.py mon flash-image <slot> --erase-tbs`.

usage: make_slot.py <crossfire.bin> <tango2-elrs.bin> <out.bin>
crossfire.bin is a TBS catalog file from https://firmware.blacksheep.com.sg
for the radio's Crossfire board, which the TBS bootloader records at 0x08007F04
(`tango2.py mon peek 0x08007F04`): 0x000140xx (Tango 2 board V1-V2, SX1272 RFO)
or 0x000141xx (board V3-V9, PA_BOOST; TBS's catalog mislabels it "Micro TX").
The wrong one radiates nothing. EdgeTX hosts exactly the 6.30 builds
(0x000140xx_0x0630.bin, 0x000141xx_0x0630.bin). A raw image linked at
0x080C0000 also works.
"""
import struct
import sys
import zlib

SLOT_ADDR = 0x080C0000
SLOT_SIZE = 0x40000
CROSSFIRE_MAX = 0x28000
CROSSFIRE_FAMILIES = (0x0140, 0x0141)
ELRS_OFFSET = 0x080E8000 - SLOT_ADDR
ELRS_MAX = SLOT_SIZE - ELRS_OFFSET
ELRS_MAGIC = 0x3252454C


def unwrap_tbs(d, path):
    """payload of a TBS catalog file: 11-byte header, payload, 1 check byte"""
    family = int.from_bytes(d[2:4], "big")
    if family not in CROSSFIRE_FAMILIES:
        sys.exit(f"{path}: TBS image for device 0x{family:04X}, not a Tango 2 Crossfire board (0x0140/0x0141)")
    n = int.from_bytes(d[8:11], "big")
    if len(d) != 11 + n + 1:
        sys.exit(f"{path}: length {len(d)} doesn't match the header's payload length {n}")
    print(f"TBS catalog image for 0x{family:04X}xx, version {d[5]:x}.{d[6]:02x}")
    return d[11:11 + n]


def crossfire(path):
    d = open(path, "rb").read()
    if len(d) == 12 + int.from_bytes(d[8:11], "big"):   # catalog header + payload + check byte
        d = unwrap_tbs(d, path)
    sp, reset = struct.unpack("<II", d[:8])
    if not (0x20000000 < sp <= 0x20020000 and SLOT_ADDR < reset < SLOT_ADDR + CROSSFIRE_MAX):
        sys.exit(f"{path}: not a Crossfire image linked at 0x080C0000 (sp {sp:08x}, reset {reset:08x})")
    used = len(d.rstrip(b"\xff"))
    if used > CROSSFIRE_MAX:
        sys.exit(f"{path}: {used} bytes, more than the {CROSSFIRE_MAX} reserved for it")
    return d[:used]


def elrs(path):
    d = open(path, "rb").read()
    magic, _, _, size, crc = struct.unpack("<IHHII", d[:16])
    if magic != ELRS_MAGIC or size != len(d) or zlib.crc32(d[60:size]) != crc:
        sys.exit(f"{path}: bad ELRS image header or CRC")
    if size > ELRS_MAX:
        sys.exit(f"{path}: {size} bytes, more than the {ELRS_MAX} available")
    return d


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    cf, el = crossfire(sys.argv[1]), elrs(sys.argv[2])
    slot = bytearray(b"\xff" * SLOT_SIZE)
    slot[:len(cf)] = cf
    slot[ELRS_OFFSET:ELRS_OFFSET + len(el)] = el
    open(sys.argv[3], "wb").write(slot)
    print(f"crossfire {len(cf)} B @0x{SLOT_ADDR:08X}, elrs {len(el)} B @0x{SLOT_ADDR + ELRS_OFFSET:08X}"
          f" -> {sys.argv[3]}")


if __name__ == "__main__":
    main()
