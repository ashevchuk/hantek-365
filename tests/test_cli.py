"""Tests for hantek365.pl against the meter emulator.

Run from the repository root:  python3 -m unittest discover -s tests
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unittest
from contextlib import contextmanager
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
SCRIPT = TESTS_DIR.parent / 'hantek365.pl'
EMULATOR = TESTS_DIR / 'meter_emulator.py'

ANSI_ESCAPE = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]')
PERL_WARNING = re.compile(r' at \S+ line \d+')


@contextmanager
def emulator(*args):
    proc = subprocess.Popen([sys.executable, '-I', str(EMULATOR), *args],
                            stdout=subprocess.PIPE, text=True)
    try:
        device = proc.stdout.readline().strip()
        if not device.startswith('/dev/pts/'):
            raise RuntimeError(f'emulator did not report a pty: {device!r}')
        yield device
    finally:
        proc.terminate()
        proc.wait(timeout=5)
        proc.stdout.close()


def run_cli(device, *args, seconds):
    """Runs the CLI with plain stdout and stops it with SIGINT like Ctrl+C."""
    proc = subprocess.Popen(['perl', str(SCRIPT), '-p', device, *args],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(seconds)
    proc.send_signal(signal.SIGINT)
    out, err = proc.communicate(timeout=10)
    return out, err


def set_winsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))


def run_tui(device, *args, size=(30, 100), resize_to=None, seconds=1.5):
    """Runs --tui on a pty of the given (rows, cols); returns (frames, raw output, stderr)."""
    master, slave = pty.openpty()
    set_winsize(master, *size)
    proc = subprocess.Popen(['perl', str(SCRIPT), '-p', device, '-g', *args],
                            stdout=slave, stderr=subprocess.PIPE)
    os.close(slave)
    out = b''

    def drain(duration):
        nonlocal out
        end = time.time() + duration
        while time.time() < end:
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                try:
                    out += os.read(master, 1 << 16)
                except OSError:
                    return

    drain(seconds)
    if resize_to:
        set_winsize(master, *resize_to)
        drain(seconds)
    proc.send_signal(signal.SIGINT)
    # Keep reading: a full pty would block perl's writes and its signal handling.
    for _ in range(100):
        if proc.poll() is not None:
            break
        drain(0.05)
    err = proc.communicate(timeout=5)[1].decode()
    os.close(master)

    text = out.decode('utf-8', 'replace')
    return text.split('\x1b[H')[1:], text, err


def tui_stats(frame):
    m = re.search(r'Min: (.+?)   Max: (.+?)\x1b', frame)
    return (m.group(1), m.group(2)) if m else None


class TuiTest(unittest.TestCase):

    def test_overload_shows_ol_without_warnings(self):
        with emulator('--script', 'ol') as dev:
            frames, _, err = run_tui(dev)
        self.assertFalse(PERL_WARNING.search(err), err)
        self.assertTrue(any('\x1b[1m  OL' in f for f in frames))
        self.assertTrue(any('\x1b[1m  +034.6' in f for f in frames))

    def test_min_max_cover_the_screen_and_restart_on_unit_change(self):
        with emulator('--script', 'tui') as dev:
            frames, _, err = run_tui(dev, size=(30, 100))
        self.assertFalse(PERL_WARNING.search(err), err)
        # 100 columns minus the 10-column axis keep 90 readings: by reading 101
        # the 9999 V spike has scrolled out.
        self.assertEqual(tui_stats(frames[100]), ('34.6 mV', '34.6 mV'))
        last = [s for s in map(tui_stats, frames) if s][-1]
        self.assertEqual(last, ('1.234 kΩ', '1.234 kΩ'))

    def test_axis_stays_aligned_with_multibyte_prefixes(self):
        with emulator('--script', 'micro') as dev:
            frames, _, _ = run_tui(dev, size=(30, 100))
        lines = ANSI_ESCAPE.sub('', frames[10]).split('\n')
        self.assertIn('µ', ''.join(lines))   # labels carry the micro sign
        axis_columns = {line.index('│') for line in lines if '│' in line}
        self.assertEqual(axis_columns, {9})

    def test_resize_redraws_the_screen(self):
        with emulator('--script', 'tui') as dev:
            _, raw, _ = run_tui(dev, size=(30, 100), resize_to=(20, 60))
        self.assertGreaterEqual(raw.count('\x1b[2J'), 2)
        self.assertIn('\x1b[K', raw)


class SettleTest(unittest.TestCase):

    def assert_no_stale_readings(self, *args):
        with emulator('--stale') as dev:
            out, err = run_cli(dev, *args, '-t', seconds=3)
        readings = [line for line in out.splitlines() if '|' in line]
        self.assertTrue(readings, err)
        self.assertFalse([r for r in readings if '9999' in r])

    def test_stale_reading_after_mode_change_is_dropped(self):
        self.assert_no_stale_readings('-m', '60VDC')

    def test_stale_reading_after_mode_and_rel_change_is_dropped(self):
        self.assert_no_stale_readings('-m', '60VDC', '-r')


class RelativeTest(unittest.TestCase):

    def check_rel(self, emu_args, cli_args, expected_message):
        with emulator('--stale', *emu_args) as dev:
            out, err = run_cli(dev, *cli_args, '-r', seconds=4)
        self.assertIn('REL', out.splitlines()[-1])
        self.assertIn(expected_message, err)

    def test_enables_rel_when_off(self):
        self.check_rel([], [], 'REL mode enabled.')

    def test_enables_rel_when_off_after_mode_change(self):
        self.check_rel([], ['-m', 'VDC'], 'REL mode enabled.')

    def test_keeps_rel_when_already_on(self):
        self.check_rel(['--rel-on'], [], 'REL mode already enabled.')

    def test_keeps_rel_when_already_on_after_mode_change(self):
        self.check_rel(['--rel-on'], ['-m', 'VDC'], 'REL mode already enabled.')


if __name__ == '__main__':
    unittest.main()
