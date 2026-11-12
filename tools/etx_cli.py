"""Run one EdgeTX CLI command on the Tango 2 (USB serial 0483:5741) and print
the reply. usage: etx_cli.py "<command>" [seconds to collect, default 1.5]"""
import sys
import time

import serial
import serial.tools.list_ports


def port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x0483 and p.pid == 0x5741:
            return p.device
    sys.exit("EdgeTX (0483:5741) not found")


def main():
    cmd = sys.argv[1]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5
    with serial.Serial(port(), 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(b"\n")  # run/clear any partial line: the CLI acts on LF only
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(cmd.encode() + b"\n")
        out = b""
        t0 = time.time()
        while time.time() - t0 < secs:
            out += s.read(4096)
    text = out.decode(errors="replace").replace("\r", "")
    print(text.strip())


if __name__ == "__main__":
    main()
