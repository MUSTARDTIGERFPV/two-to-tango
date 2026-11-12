"""Talk to the TBS Tango 2 bootloader (USB HID 04D8:F94C) and the resident loader.

  python -P tango2.py ping                 read-only: identify the bootloader
  python -P tango2.py flash <app.bin>      write an app image to 0x0800C000 (Wingman's protocol)
  python -P tango2.py mon [COMx] info|fault|reset
  python -P tango2.py mon [COMx] peek <addr> [n] | poke <addr> <val> [width]
  python -P tango2.py mon [COMx] flash-app <edgetx.bin>      EdgeTX linked at 0x08020000
  python -P tango2.py mon [COMx] flash-image <slot.bin> --erase-tbs     radio images (make_slot.py) at 0x080C0000
  python -P tango2.py mon [COMx] boot                        leave the loader, start EdgeTX
  python -P tango2.py mon [COMx] image [secs]                host the ELRS image, show its logs,
                                                             ping it (Ctrl-C or secs to stop)

The loader's port is found by USB ID 0483:5740 when COMx is omitted.

The TBS bootloader protocol was worked out from the Wingman web flasher.
The bootloader itself (0x08000000-0x0800BFFF) is never written by this protocol.
"""
import os
import struct
import sys
import time
import zlib

VID, PID = 0x04D8, 0xF94C
HW_ID = 0x00040001
VERSION_FIELD = 0x0128
WINDOW, BLOCK = 6144, 48
APP_START, APP_END = 0x0800C000, 0x080BFFFF
MAX_SIZE = 0xB4000
ADDR_HOST, ADDR_BL = 0x10, 0xEA
T_PING, T_INFO, T_FW = 0x28, 0x29, 0x38


def crc8_d5(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0xD5) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def _reflect(v, n):
    r = 0
    for _ in range(n):
        r = (r << 1) | (v & 1)
        v >>= 1
    return r


def tbs_crc32(data):
    # Wingman's image CRC. Looks like CRC-32 but feeds back (top bit OR data bit),
    # not XOR, so it differs from zlib. Kept bit-exact with the flasher.
    e = 0xFFFFFFFF
    for b in data:
        l = _reflect(b, 8)
        for _ in range(8):
            fb = (e & 0x80000000) or (l & 0x80)
            e = ((e << 1) ^ (0x04C11DB7 if fb else 0)) & 0xFFFFFFFF
            l = (l << 1) & 0xFF
    return _reflect(e, 32) ^ 0xFFFFFFFF


def frame(ftype, dest, orig, payload=b""):
    body = bytes([ftype, dest, orig]) + bytes(payload)
    return bytes([0xC8, len(body) + 1]) + body + bytes([crc8_d5(body)])


KEEPALIVE = bytes([0xC8, 0x07, 0x32, 0xEA, 0x10, 0x0A, 0x04, 0xC6, 0x7E])


def parse(report):
    r = bytes(report)
    if len(r) < 5 or r[0] != 0xC8:
        return None
    end = r[1] + 2
    if end > len(r) or end < 5:
        return None
    f = r[:end]
    if crc8_d5(f[2:end - 1]) != f[end - 1]:
        return None
    return {"type": f[2], "dest": f[3], "orig": f[4], "body": f[5:end - 1]}


class Bootloader:
    def __init__(self):
        import hid
        self.dev = hid.device()
        self.dev.open(VID, PID)
        self.dev.set_nonblocking(True)
        self.info = self.dev.get_product_string()

    def send(self, data):
        buf = bytes(data) + bytes(64 - len(data))
        self.dev.write(b"\x00" + buf)

    def recv(self):
        r = self.dev.read(64)
        return parse(r) if r else None

    def close(self):
        self.dev.close()


def device_info(body):
    z = body.find(b"\x00")
    if z < 0 or z + 13 > len(body):
        return None
    serial, hwid, fwid = struct.unpack_from(">III", body, z + 1)
    return body[:z].decode(errors="replace"), serial, hwid, fwid


def cmd_ping():
    bl = Bootloader()
    print("opened:", bl.info)
    bl.send(frame(T_PING, 0x00, 0x0E))
    t0 = time.time()
    seen = 0
    while time.time() - t0 < 2.0:
        f = bl.recv()
        if not f:
            time.sleep(0.005)
            continue
        seen += 1
        if f["type"] == T_INFO:
            info = device_info(f["body"])
            if info:
                name, serial, hwid, fwid = info
                print("device info: name=%r origin=0x%02X serial=0x%08X hwId=0x%08X fwId=0x%08X extra=%s"
                      % (name, f["orig"], serial, hwid, fwid, f["body"][len(name) + 13:].hex()))
                continue
        print("frame type=0x%02X dest=0x%02X orig=0x%02X body=%s" % (f["type"], f["dest"], f["orig"], f["body"].hex()))
    if not seen:
        print("no answer")
    bl.close()


def check_image(img):
    if len(img) < 8 or len(img) > MAX_SIZE:
        sys.exit("bad size %d" % len(img))
    sp, rv = struct.unpack_from("<II", img, 0)
    if not (sp == 0x10010000 or 0x20000000 <= sp <= 0x20020000):
        sys.exit("initial SP 0x%08X outside RAM/CCM" % sp)
    if rv % 2 == 0 or not (APP_START <= rv - 1 <= APP_END):
        sys.exit("reset vector 0x%08X not a Thumb address in the app area" % rv)


def cmd_flash(path):
    img = open(path, "rb").read()
    check_image(img)
    padded = img + b"\xFF" * (-len(img) % WINDOW)
    size = len(padded)
    crc = tbs_crc32(padded)
    print("image %s: %d bytes, padded %d, crc 0x%08X" % (path, len(img), size, crc))
    if input('type FLASH to write it to 0x0800C000: ').strip() != "FLASH":
        sys.exit("aborted")

    bl = Bootloader()
    bl.send(frame(T_PING, 0x00, 0x0E))
    t0 = time.time()
    ok = False
    while time.time() - t0 < 2.0 and not ok:
        f = bl.recv()
        if f and f["type"] == T_INFO:
            info = device_info(f["body"])
            ok = bool(info and info[0] == "TBS BOOTLOADER" and f["orig"] == ADDR_BL
                      and info[3] == 0 and info[2] >> 8 == HW_ID >> 8)
        elif not f:
            time.sleep(0.005)
    if not ok:
        sys.exit("bootloader did not identify as TBS BOOTLOADER / hwId 0x000400xx; nothing sent")

    last_ka = 0.0

    def keepalive():
        nonlocal last_ka
        if time.time() - last_ka > 1.0:
            last_ka = time.time()
            bl.send(KEEPALIVE)

    start = struct.pack(">BBIHII", 1, 0, HW_ID, VERSION_FIELD, size, crc)
    bl.send(frame(T_FW, ADDR_BL, ADDR_HOST, start))
    last_window, last_confirm = -1, -1
    last_rx = time.time()
    try:
        while True:
            keepalive()
            f = bl.recv()
            if not f:
                if time.time() - last_rx > 20:
                    sys.exit("no answer from the radio for 20 s (still in bootloader; retry)")
                time.sleep(0.005)
                continue
            last_rx = time.time()
            if f["type"] != T_FW or not f["body"]:
                continue
            sub, rest = f["body"][0], f["body"][1:]
            if sub == 7:
                print("\rerase %3d%%" % (rest[0] if rest else 0), end="", flush=True)
            elif sub == 2 and len(rest) >= 12:
                off, x, dcrc = struct.unpack_from(">III", rest, 0)
                if off == 0 and x == 0:
                    print("\ndone: device crc 0x%08X %s" % (dcrc, "OK" if dcrc == crc else "MISMATCH"))
                    return
                if off == last_window:
                    continue
                if off % WINDOW or off + WINDOW > size:
                    sys.exit("radio asked for window 0x%X outside the image" % off)
                for i in range(WINDOW // BLOCK):
                    a = off + i * BLOCK
                    bl.send(frame(T_FW, ADDR_BL, ADDR_HOST, bytes([3, i, 0x80]) + padded[a:a + BLOCK]))
                    time.sleep(0.020)
                    keepalive()
                last_window = off
                print("\rwrite %3d%% (window 0x%06X)" % (100 * off // size, off), end="", flush=True)
            elif sub == 5 and len(rest) >= 21:
                blank = all(b == 0xFF for b in rest[:16])
                addr = struct.unpack_from(">I", rest, 17)[0]
                if blank and addr != last_confirm and addr == last_window:
                    last_confirm = addr
                    bl.send(frame(T_FW, ADDR_BL, ADDR_HOST, b"\x04"))
    finally:
        bl.close()



MON_VID, MON_PID = 0x0483, 0x5740


APP_PID = 0x5741  # EdgeTX on the Tango 2


def _ports(pid):
    import serial.tools.list_ports
    return [p.device for p in serial.tools.list_ports.comports() if (p.vid, p.pid) == (MON_VID, pid)]


def find_monitor_port(wait=0.0):
    """The loader's port. If EdgeTX is running instead, ask it to reboot into
    the loader (CLI command "loader") and wait for the loader to enumerate."""
    import serial
    ports = _ports(MON_PID)
    if not ports and _ports(APP_PID):
        app = _ports(APP_PID)[0]
        print("EdgeTX on %s: sending 'loader'" % app)
        with serial.Serial(app, 115200, timeout=1) as s:
            s.write(b"\r\nloader\r\n")
            s.flush()
        wait = max(wait, 10)
    t_end = time.time() + wait
    while not ports and time.time() < t_end:
        time.sleep(0.5)
        ports = _ports(MON_PID)
    if len(ports) != 1:
        sys.exit("expected one loader port (0483:5740), found %s" % (ports or "none"))
    return ports[0]


class Monitor:
    def __init__(self, port=None):
        import serial
        self.s = serial.Serial(port or find_monitor_port(), 115200, timeout=3)
        self.s.reset_input_buffer()

    def cmd(self, line, echo=True):
        self.s.write(line.encode() + b"\n")
        out = []
        while True:
            l = self.s.readline()
            if not l:
                sys.exit("monitor did not answer %r" % line)
            l = l.decode(errors="replace").rstrip("\r\n")
            if l == "OK":
                return out
            if l.startswith("ERR"):
                sys.exit("%s -> %s" % (line, l))
            out.append(l)
            if echo:
                print(l)

    def flash(self, path, addr, erase_tbs=False):
        """Erase + program + CRC-check a file in the loader-managed flash (0x08020000+)."""
        img = open(path, "rb").read()
        img += b"\xFF" * (-len(img) % 4)
        sector_bytes = 0x20000
        erase_len = -(-len(img) // sector_bytes) * sector_bytes
        print("erase 0x%08X +0x%X" % (addr, erase_len))
        self.s.timeout = 30
        self.cmd("E 0x%08X 0x%X%s" % (addr, erase_len, " ERASE-TBS" if erase_tbs else ""))
        # The first 8 bytes (initial SP + reset vector) go last, so an
        # interrupted write never leaves something the loader would boot.
        t0 = time.time()
        self._write(addr + 8, img[8:])
        self._write(addr, img[:8])
        print("\rwrite done in %.1f s, %d bytes, crc %08X" % (time.time() - t0, len(img), zlib.crc32(img) & 0xFFFFFFFF))

    def _write(self, addr, data):
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.s.timeout = 5
        self.s.write(b"W 0x%08X %d 0x%08X\n" % (addr, len(data), crc))
        if self.s.readline().strip() != b"READY":
            sys.exit("loader refused the write")
        for i in range(0, len(data), 64):
            # STM32duino's CDC stalls on back-to-back data, and the loader stalls
            # while programming; pace the chunks.
            self.s.write(data[i:i + 64])
            time.sleep(0.002)
            if i % 0x10000 == 0 and len(data) > 0x10000:
                print("\rwrite %3d%%" % (100 * i // len(data)), end="", flush=True)
        reply = self.s.readline().decode(errors="replace").strip()
        if reply != "OK":
            sys.exit("write at 0x%08X failed: %s" % (addr, reply))


class ImageLink:
    """Framed CRSF link to the ELRS image hosted by the loader ('G' command).

    send(frame) sends a complete CRSF frame; poll() returns a list of
    ("crsf", bytes) / ("log", str) / ("status", str) items received so far.
    """

    def __init__(self, monitor):
        self.m = monitor
        self.s = monitor.s
        self.buf = bytearray()
        self.s.reset_input_buffer()
        self.s.write(b"G\n")
        self.s.timeout = 3
        line = self.s.readline().decode(errors="replace").strip()
        if not line.startswith("IMAGE"):
            sys.exit("loader: %s" % (line or "no answer to G"))
        self.banner = line
        self.s.timeout = 0.02

    @staticmethod
    def frame(sync, ftype, payload=b""):
        body = bytes([ftype]) + bytes(payload)
        return bytes([sync, len(body) + 1]) + body + bytes([crc8_d5(body)])

    def send(self, frame):
        self.s.write(frame)

    def poll(self):
        self.buf += self.s.read(4096)
        out = []
        while len(self.buf) >= 4:
            sync, n = self.buf[0], self.buf[1]
            if sync not in (0xC8, 0xEA, 0xEE, 0xEC, 0x7E) or n < 2 or n > 62:
                del self.buf[0]
                continue
            if len(self.buf) < n + 2:
                break
            f = bytes(self.buf[:n + 2])
            del self.buf[:n + 2]
            if crc8_d5(f[2:-1]) != f[-1]:
                continue
            if sync == 0x7E:
                text = f[3:-1].decode(errors="replace")
                out.append(("log" if f[2] == 0x7F else "status", text))
            else:
                out.append(("crsf", f))
        return out

    def stop(self):
        self.s.write(self.frame(0x7E, 0x01))
        self.s.timeout = 3
        tail = self.s.read_until(b"OK\n")
        for kind, text in self._decode_tail(tail):
            print("[%s] %s" % (kind, text))

    def _decode_tail(self, data):
        self.buf += data.replace(b"OK\n", b"")
        return [x for x in self.poll() if x[0] != "crsf"]


def cmd_image(m, secs):
    link = ImageLink(m)
    print(link.banner)
    t_end = time.time() + secs
    t_ping = 0
    try:
        while time.time() < t_end:
            if time.time() - t_ping > 2:
                t_ping = time.time()
                link.send(ImageLink.frame(0xEE, 0x28, b"\x00\xEA"))  # device ping, to all, from handset
            for kind, item in link.poll():
                if kind == "crsf":
                    if item[2] == 0x29:
                        name = item[5:item.index(0, 5)].decode(errors="replace")
                        print("[device info] %s" % name)
                    else:
                        print("[crsf] %s" % item.hex(" "))
                else:
                    print("[%s] %s" % (kind, item))
    except KeyboardInterrupt:
        pass
    link.stop()


LOADER_STAGING = 0x20010000  # in the task area, above the updater task itself



def cmd_mon(argv):
    port = None
    if argv and argv[0].upper().startswith("COM"):
        port, argv = argv[0], argv[1:]
    if not argv:
        sys.exit(__doc__)
    m = Monitor(port)
    op, rest = argv[0], argv[1:]
    if op == "info":
        m.cmd("I")
    elif op == "fault":
        m.cmd("F")
    elif op == "reset":
        m.cmd("Z")
    elif op == "peek":
        m.cmd("X %s %s" % (rest[0], rest[1] if len(rest) > 1 else "1"))
    elif op == "poke":
        m.cmd("Y %s %s %s" % (rest[0], rest[1], rest[2] if len(rest) > 2 else "4"))
    elif op == "boot":
        m.cmd("B")
    elif op == "image":
        cmd_image(m, float(rest[0]) if rest else 10)
    elif op == "flash-app":
        m.flash(rest[0], 0x08020000)
    elif op == "flash-image":
        m.flash(rest[0], 0x080C0000, erase_tbs="--erase-tbs" in rest)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "mon":
        cmd_mon(sys.argv[2:])
        sys.exit(0)
    if cmd == "ping":
        cmd_ping()
    elif cmd == "flash" and len(sys.argv) == 3:
        cmd_flash(sys.argv[2])
    else:
        sys.exit(__doc__)
