"""Copy a file onto the Tango 2's SD card through the EdgeTX CLI, then verify it
with `dump`s CRC-32. usage: sd_put.py <local file> <SD path>
From Git Bash run it with MSYS_NO_PATHCONV=1, or "/CROSSFIRE/..." becomes
"C:/Program Files/Git/CROSSFIRE/..." before Python sees it.

Uses `fwrite <path> <offset> <hex>` (one command line per 96 bytes): slower than
the binary `fput`, but a bad command line can never leave raw file bytes in the
CLI's line parser. Needs CLI_FILE_ACCESS.
"""
import re
import sys
import time
import zlib

import serial
import serial.tools.list_ports

CHUNK = 96  # 192 hex chars + path + offset stays under CLI_COMMAND_MAX_LEN (256)


def port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x0483 and p.pid == 0x5741:
            return p.device
    sys.exit("EdgeTX (0483:5741) not found")


def until(s, pattern, secs):
    out, t0 = b"", time.time()
    while time.time() - t0 < secs:
        out += s.read(4096)
        m = re.search(pattern, out)
        if m:
            return m, out
    return None, out


def main():
    data = open(sys.argv[1], "rb").read()
    path = sys.argv[2].encode()
    t0 = time.time()
    with serial.Serial(port(), 115200, timeout=0.02) as s:
        s.write(b"\n")
        time.sleep(0.3)
        s.reset_input_buffer()
        for off in range(0, len(data), CHUNK):
            blk = data[off:off + CHUNK]
            s.write(b"fwrite %s %d %s\n" % (path, off, blk.hex().encode()))
            m, out = until(s, rb"(OK \d+|ERR[^\r\n]*|Invalid[^\r\n]*)", 5)
            if not m or m.group(1) != b"OK %d" % len(blk):
                sys.exit("fwrite at %d failed: %r" % (off, out[-200:]))
            if off % (CHUNK * 200) == 0:
                print("\r%3d%%" % (100 * off // len(data)), end="", flush=True)
        print("\rwritten %d bytes in %.0f s" % (len(data), time.time() - t0))
        s.reset_input_buffer()
        s.write(b"dump %s\n" % path)
        m, out = until(s, rb"crc32 ([0-9a-fA-F]+)", 900)
        want = zlib.crc32(data) & 0xFFFFFFFF
        size = re.search(rb"size (\d+)", out)
        if not m:
            sys.exit("no crc32 from dump")
        got = int(m.group(1), 16)
        print("on card: size %s, crc32 %08x, expected %08x: %s" % (
            size.group(1).decode() if size else "?", got, want, "OK" if got == want else "MISMATCH"))


if __name__ == "__main__":
    main()
