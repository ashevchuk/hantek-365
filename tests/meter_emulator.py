#!/usr/bin/env python3
"""Fake Hantek 365 on a pseudo-terminal, for tests without hardware.

Prints the pty device path on the first stdout line, then serves the
meter protocol until the client goes away or nothing is received for 30 s.

Default behaviour follows the real meter: 03 XX is acknowledged with DD
and selects the unit of the readings, 03 F3 toggles the REL flag, 01 0F is
answered with a 15-byte measurement packet.

  --no-ack HEX   leave this mode code unacknowledged (mode change timeout)
  --rel-on       start with REL enabled
  --stale        after any 03 XX the first reading is garbage, as the real
                 meter reports its previous display with the new range
  --script NAME  ignore the mode and answer polls with a fixed sequence:
                   tui      9999 V spike, 100 x 34.6 mV, then 1234 ohm
                   ol       34.6 mV and a synthetic overload in volts, alternating
                   ol-real  an overload packet captured from a real 365F
                   micro    34.6 uA, rising slowly (multi-byte SI prefix)
"""
import argparse
import os
import pty
import select
import tty


def packet(digits, dpos, flags, mult, unit, sign=0x2B):
    return (bytes([0xA0, sign]) + digits
            + bytes([0x20, dpos, flags, 0x00, mult, unit, 0x00, 0x0D, 0x0A]))


MILLIVOLTS = packet(b"0346", 0x34, 0x31, 0x40, 0x80)
SPIKE      = packet(b"9999", 0x30, 0x31, 0x00, 0x80)
OHMS       = packet(b"1234", 0x30, 0x21, 0x00, 0x20)
OL_VOLTS   = packet(b"????", 0x31, 0x31, 0x00, 0x80)
OL_REAL    = bytes.fromhex("a02b3f303a3f2034210000203d0d0a")
MICROAMPS  = [packet(d, 0x34, 0x31, 0x80, 0x40) for d in (b"0346", b"0347", b"0348")]

SCRIPTS = {
    'tui':     [SPIKE] + [MILLIVOLTS] * 100 + [OHMS],
    'ol':      [MILLIVOLTS, OL_VOLTS],
    'ol-real': [OL_REAL],
    'micro':   MICROAMPS,
}

# high nibble of the mode code -> (flags byte, unit byte)
UNIT_BY_NIBBLE = {
    0xA: (0x31, 0x80),  # DC V
    0xB: (0x29, 0x80),  # AC V
    0xC: (0x31, 0x40),  # DC A
    0xD: (0x29, 0x40),  # AC A
    0xE: (0x21, 0x20),  # ohm
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-ack', type=lambda s: int(s, 16))
    ap.add_argument('--rel-on', action='store_true')
    ap.add_argument('--stale', action='store_true')
    ap.add_argument('--script', choices=SCRIPTS)
    args = ap.parse_args()

    mode, rel, stale = 0xA0, args.rel_on, False
    script_pos = 0

    master, slave = pty.openpty()
    tty.setraw(slave)
    print(os.ttyname(slave), flush=True)

    buf = b""
    while True:
        ready, _, _ = select.select([master], [], [], 30)
        if not ready:
            break
        try:
            buf += os.read(master, 64)
        except OSError:
            break
        while len(buf) >= 2:
            cmd, buf = buf[:2], buf[2:]
            if cmd[0] == 0x03:
                if cmd[1] == args.no_ack:
                    continue
                if cmd[1] == 0xF3:
                    rel = not rel
                elif (cmd[1] >> 4) in UNIT_BY_NIBBLE:
                    mode = cmd[1]
                os.write(master, b"\xdd")
                stale = args.stale
            elif cmd == b"\x01\x0f":
                if args.script:
                    seq = SCRIPTS[args.script]
                    if args.script == 'tui':
                        os.write(master, seq[min(script_pos, len(seq) - 1)])
                    else:
                        os.write(master, seq[script_pos % len(seq)])
                    script_pos += 1
                elif stale:
                    stale = False
                    os.write(master, SPIKE)
                else:
                    flags, unit = UNIT_BY_NIBBLE[mode >> 4]
                    if rel:
                        flags = (flags | 0x04) & ~0x20
                    os.write(master, packet(b"0346", 0x34, flags, 0x40, unit))


if __name__ == '__main__':
    main()
