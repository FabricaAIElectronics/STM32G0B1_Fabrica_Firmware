#!/usr/bin/env python3
"""V5.2 ATtiny-protocol host exerciser for the ButtonBoard V5.5 STM32 slave.

Runs the exact byte sequences the Jetson-side V5.2 code uses, against the
STM32 at 0x51. Bus defaults to the Pi's i2c-1 (GPIO2/3); override with
--bus N. Usage:
    ./pi_i2c_host_emu.py poll [--n 30]   # READ_ALL loop, prints decoded fields
    ./pi_i2c_host_emu.py write-enc -100  # RW_ENCODER preset (LE), then reads back
    ./pi_i2c_host_emu.py version         # READ_VERSION -> (major, minor)
    ./pi_i2c_host_emu.py uid             # READ_UID -> 10 bytes
    ./pi_i2c_host_emu.py overread        # read 40 bytes after READ_ALL: bytes 7.. must be 0xFF
    ./pi_i2c_host_emu.py all             # version, uid, poll x5, write-enc, overread, poll x5
"""
import argparse, struct, sys, time
from smbus2 import SMBus, i2c_msg

ADDR = 0x51
CMD_READ_ALL, CMD_RW_ENC, CMD_VER, CMD_UID = 0x09, 0x80, 0xFE, 0x10


def xfer(bus, cmd, nread, payload=b""):
    """Write cmd(+payload); if nread, repeated-START read - the ATtiny pattern."""
    w = i2c_msg.write(ADDR, bytes([cmd]) + payload)
    if nread == 0:
        bus.i2c_rdwr(w)
        return b""
    r = i2c_msg.read(ADDR, nread)
    bus.i2c_rdwr(w, r)
    return bytes(r)


def read_all(bus):
    b = xfer(bus, CMD_READ_ALL, 7)
    knob, ref, enc = struct.unpack(">HHh", b[:6])      # big-endian, as the ATtiny sent
    return knob, ref, enc, b[6]


def do_poll(bus, n):
    for _ in range(n):
        knob, ref, enc, btn = read_all(bus)
        print(f"knob={knob:4d}mV ref={ref}mV enc={enc:6d} btn={btn}", flush=True)
        time.sleep(0.1)


def do_write_enc(bus, val):
    xfer(bus, CMD_RW_ENC, 0, struct.pack("<h", val))       # LE write - the asymmetry
    time.sleep(0.05)
    (enc,) = struct.unpack(">h", xfer(bus, CMD_RW_ENC, 2))  # BE read
    print(f"wrote {val}, read back {enc}", "OK" if enc == val else "MISMATCH")
    return enc == val


def do_overread(bus):
    b = xfer(bus, CMD_READ_ALL, 40)
    ok = all(x == 0xFF for x in b[7:])
    print("first 7:", b[:7].hex(), "| pad all 0xFF:", ok)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("op", nargs="?", default="poll")
    ap.add_argument("value", nargs="?", type=int, default=-100)
    ap.add_argument("--bus", type=int, default=1)
    ap.add_argument("--n", type=int, default=30)
    a = ap.parse_args()
    with SMBus(a.bus) as bus:
        if a.op == "poll":
            do_poll(bus, a.n)
        elif a.op == "write-enc":
            do_write_enc(bus, a.value)
        elif a.op == "version":
            print("version", tuple(xfer(bus, CMD_VER, 2)))
        elif a.op == "uid":
            print("uid", xfer(bus, CMD_UID, 10).hex())
        elif a.op == "overread":
            do_overread(bus)
        elif a.op == "all":
            print("version", tuple(xfer(bus, CMD_VER, 2)))
            print("uid    ", xfer(bus, CMD_UID, 10).hex())
            print("uid2   ", xfer(bus, CMD_UID, 10).hex(), "(must match)")
            do_poll(bus, 5)
            do_write_enc(bus, a.value)
            do_write_enc(bus, 222)
            do_overread(bus)
            print("post-overread poll:")
            do_poll(bus, 5)
        else:
            sys.exit(__doc__)


if __name__ == "__main__":
    main()
