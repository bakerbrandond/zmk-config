#!/usr/bin/env python3
"""Send the local time to the Prospector dongle's clock over Raw HID (Linux).

Finds the dongle's Raw HID interface (ZMK 1D50:615E, usage page 0xFF60,
usage 0x61) among /dev/hidraw*, over USB or Bluetooth, and writes the
qmk-hid-host TIME packet [0xAA, hour, minute] when it connects and whenever
the minute changes. Keeps looking for the dongle while it's unplugged.
Standard library only. Needs write access to the hidraw node; see the udev
rule in the README.

    scripts/hid-clock.py            # run until stopped
    scripts/hid-clock.py --once     # send the time once and exit
"""
import argparse
import glob
import os
import sys
import time

VID, PID = 0x1D50, 0x615E
USAGE_PAGE_AND_USAGE = bytes([0x06, 0x60, 0xFF, 0x09, 0x61])
REPORT_SIZE = 32
TIME = 0xAA
RESCAN_S = 5


def find_dongle():
    """Path of the dongle's Raw HID node, or None."""
    for node in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            with open(os.path.join(node, "device", "uevent")) as f:
                uevent = f.read()
            with open(os.path.join(node, "device", "report_descriptor"), "rb") as f:
                desc = f.read()
        except OSError:
            continue
        hid_id = next((l.split("=", 1)[1] for l in uevent.splitlines() if l.startswith("HID_ID=")), "")
        parts = hid_id.split(":")  # bus:vendor:product, e.g. 0003:00001D50:0000615E
        if len(parts) != 3 or int(parts[1], 16) != VID or int(parts[2], 16) != PID:
            continue
        if desc.startswith(USAGE_PAGE_AND_USAGE):
            return "/dev/" + os.path.basename(node)
    return None


def send_time(fd, now):
    # Leading 0: no report IDs on this interface
    report = bytes([0, TIME, now.tm_hour, now.tm_min]).ljust(1 + REPORT_SIZE, b"\0")
    os.write(fd, report)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--once", action="store_true", help="send the time once and exit")
    args = ap.parse_args()

    fd, path, sent = None, None, None
    warned = False
    while True:
        if fd is None:
            path = find_dongle()
            if path is None:
                if args.once:
                    sys.exit("dongle not found")
                time.sleep(RESCAN_S)
                continue
            try:
                fd = os.open(path, os.O_WRONLY)
            except PermissionError:
                if args.once or not warned:
                    print(f"no permission to open {path}; install the udev rule", file=sys.stderr)
                    warned = True
                if args.once:
                    sys.exit(1)
                time.sleep(RESCAN_S)
                continue
            print(f"sending time to {path}", flush=True)
            sent, warned = None, False

        now = time.localtime()
        if (now.tm_hour, now.tm_min) != sent:
            try:
                send_time(fd, now)
                sent = (now.tm_hour, now.tm_min)
            except OSError as e:
                print(f"{path}: {e}; looking for the dongle again", file=sys.stderr, flush=True)
                os.close(fd)
                fd = None
                continue
        if args.once:
            print(f"sent {now.tm_hour:02d}:{now.tm_min:02d}")
            return
        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
