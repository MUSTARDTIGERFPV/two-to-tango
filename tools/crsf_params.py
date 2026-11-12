"""Drive a CRSF device's parameter menu on the Tango 2 through the EdgeTX CLI
(`rf send` / `rf cap`, radio_host.cpp).

  crsf_params.py list [dev]              names and types of every parameter
  crsf_params.py read <dev> <idx>        one parameter, raw chunk 0
  crsf_params.py write <dev> <idx> <v>   write a value (COMMAND: 1 = start)
dev defaults to 0xEE (the TX module). Hex or decimal numbers.
"""
import sys
import time

import serial
import serial.tools.list_ports

TYPES = {0: "u8", 1: "i8", 2: "u16", 3: "i16", 8: "float", 9: "select",
         10: "string", 11: "folder", 12: "info", 13: "command", 14: "out-of-range"}


def port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x0483 and p.pid == 0x5741:
            return p.device
    sys.exit("EdgeTX (0483:5741) not found")


class Cli:
    def __init__(self):
        self.s = serial.Serial(port(), 115200, timeout=0.1)
        self.cmd("")

    def cmd(self, line, secs=0.6):
        self.s.reset_input_buffer()
        self.s.write(line.encode() + b"\n")
        out, t0 = b"", time.time()
        while time.time() - t0 < secs:
            out += self.s.read(4096)
            if out.rstrip().endswith(b">") and b"end" in out or (line and out.rstrip().endswith(b">") and b"sent" in out):
                break
        return out.decode(errors="replace").replace("\r", "")

    def frames(self):
        out = self.cmd("rf cap", 1.5)
        res = []
        for l in out.splitlines():
            l = l.strip()
            if len(l) >= 8 and len(l) % 2 == 0 and all(c in "0123456789abcdef" for c in l):
                res.append(bytes.fromhex(l))
        return res

    def send(self, dev, typ, *payload):
        args = " ".join("%02x" % b for b in payload)
        self.cmd("rf send %02x %02x %s" % (dev, typ, args))


def read_param(c, dev, idx, chunk=0):
    """chunk `chunk` of parameter idx: (chunks_remaining, data) or None"""
    for _ in range(3):
        c.frames()  # drop stale
        c.send(dev, 0x2C, idx, chunk)
        time.sleep(0.15)
        for f in c.frames():
            if f[2] == 0x2B and f[4] == dev and f[5] == idx:
                return f[6], f[7:-1]
    return None


def read_full(c, dev, idx):
    first = read_param(c, dev, idx)
    if not first:
        return None
    left, data = first
    chunk = 1
    while left:
        r = read_param(c, dev, idx, chunk)
        if not r:
            break
        left, more = r
        data += more
        chunk += 1
    return data


def describe(data):
    parent, typ = data[0], data[1] & 0x3F
    name = data[2:data.index(0, 2)].decode(errors="replace")
    rest = data[data.index(0, 2) + 1:]
    extra = ""
    if typ == 9:  # select: options\0 value min max default unit
        opts = rest[:rest.index(0)].decode(errors="replace")
        val = rest[rest.index(0) + 1]
        extra = "= %d of [%s]" % (val, opts.replace(";", "|"))
    elif typ == 13:  # command: status timeout info
        extra = "status %d info '%s'" % (rest[0], rest[2:].split(b"\0")[0].decode(errors="replace"))
    elif typ in (10, 12):
        extra = "'%s'" % rest.split(b"\0")[0].decode(errors="replace")
    return "parent %d %-8s %-24s %s" % (parent, TYPES.get(typ, typ), name, extra)


def main():
    a = sys.argv[1:]
    c = Cli()
    num = lambda x: int(x, 0)
    if not a or a[0] == "list":
        dev = num(a[1]) if len(a) > 1 else 0xEE
        c.frames()
        c.send(0x00, 0x28)
        time.sleep(0.3)
        count = None
        for f in c.frames():
            if f[2] == 0x29 and f[4] == dev:
                body = f[5:-1]
                name = body[:body.index(0)].decode()
                count = body[body.index(0) + 13]
                print("device 0x%02X '%s', %d parameters" % (dev, name, count))
        if count is None:
            sys.exit("no device info from 0x%02X" % dev)
        for i in range(1, count + 1):
            d = read_full(c, dev, i)
            print("%3d %s" % (i, describe(d) if d else "(no reply)"), flush=True)
    elif a[0] == "read":
        d = read_full(c, num(a[1]), num(a[2]))
        print(describe(d) if d else "(no reply)")
        print(d.hex() if d else "")
    elif a[0] == "write":
        dev, idx, val = num(a[1]), num(a[2]), num(a[3])
        c.frames()
        c.send(dev, 0x2D, idx, val)
        time.sleep(0.5)
        d = read_full(c, dev, idx)
        print(describe(d) if d else "(no reply)")


if __name__ == "__main__":
    main()
