#!/usr/bin/env python3
"""HC32 CherryUSB Vendor Bulk descriptor and loopback check."""

import argparse
import ctypes
import ctypes.util
import itertools
import subprocess
import time

DEFAULT_VID = 0xFFFE
DEFAULT_PID = 0xFFFF
DEFAULT_LENGTHS = (0, 1, 63, 64, 65, 512, 1024)
OUT_EP = 0x02
IN_EP = 0x81
OUT_MPS = 64
OUT_RECEIVE_SIZE = 1024


def parse_lengths(value):
    lengths = tuple(int(item, 0) for item in value.split(","))
    if not lengths or any(length < 0 or length > 1024 for length in lengths):
        raise argparse.ArgumentTypeError("lengths must be in the range 0..1024")
    return lengths


def payload(sequence, length):
    return bytes((sequence + offset) & 0xFF for offset in range(length))


def needs_out_zlp(length):
    return 0 < length < OUT_RECEIVE_SIZE and length % OUT_MPS == 0


class UsbDevice:
    def __init__(self, vid, pid, timeout_ms):
        library = ctypes.util.find_library("usb-1.0")
        if not library:
            raise RuntimeError("libusb-1.0 is not installed")
        self.usb = ctypes.CDLL(library)
        self.timeout_ms = timeout_ms
        self.context = ctypes.c_void_p()
        self.handle = ctypes.c_void_p()
        self._declare_api()

        result = self.usb.libusb_init(ctypes.byref(self.context))
        if result != 0:
            raise RuntimeError(f"libusb_init failed: {result}")
        self.handle = self.usb.libusb_open_device_with_vid_pid(self.context, vid, pid)
        if not self.handle:
            self.close()
            raise RuntimeError(f"USB device {vid:04x}:{pid:04x} not found")
        self.usb.libusb_set_auto_detach_kernel_driver(self.handle, 1)
        result = self.usb.libusb_claim_interface(self.handle, 0)
        if result != 0:
            self.close()
            raise RuntimeError(f"cannot claim interface 0: libusb error {result}")

    def _declare_api(self):
        self.usb.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        self.usb.libusb_init.restype = ctypes.c_int
        self.usb.libusb_exit.argtypes = [ctypes.c_void_p]
        self.usb.libusb_open_device_with_vid_pid.argtypes = [
            ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16
        ]
        self.usb.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
        self.usb.libusb_set_auto_detach_kernel_driver.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.usb.libusb_claim_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.usb.libusb_release_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.usb.libusb_close.argtypes = [ctypes.c_void_p]
        self.usb.libusb_bulk_transfer.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ubyte,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.c_uint,
        ]

    def close(self):
        if self.handle:
            self.usb.libusb_release_interface(self.handle, 0)
            self.usb.libusb_close(self.handle)
            self.handle = ctypes.c_void_p()
        if self.context:
            self.usb.libusb_exit(self.context)
            self.context = ctypes.c_void_p()

    def transfer(self, data):
        write_buffer = (ctypes.c_ubyte * max(1, len(data)))()
        if data:
            write_buffer[:len(data)] = data
        written = ctypes.c_int()
        result = self.usb.libusb_bulk_transfer(
            self.handle, OUT_EP, write_buffer, len(data), ctypes.byref(written), self.timeout_ms
        )
        if result != 0 or written.value != len(data):
            raise RuntimeError(f"bulk OUT failed: result={result}, bytes={written.value}")
        if needs_out_zlp(len(data)):
            written = ctypes.c_int()
            result = self.usb.libusb_bulk_transfer(
                self.handle, OUT_EP, write_buffer, 0, ctypes.byref(written), self.timeout_ms
            )
            if result != 0 or written.value != 0:
                raise RuntimeError(f"bulk OUT ZLP failed: result={result}, bytes={written.value}")

        read_size = max(1, len(data))
        read_buffer = (ctypes.c_ubyte * read_size)()
        received = ctypes.c_int()
        result = self.usb.libusb_bulk_transfer(
            self.handle, IN_EP, read_buffer, read_size, ctypes.byref(received), self.timeout_ms
        )
        if result != 0:
            raise RuntimeError(f"bulk IN failed: libusb error {result}")
        return bytes(read_buffer[:received.value])

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


def show_descriptors(args):
    result = subprocess.run(
        ["lsusb", "-v", "-d", f"{args.vid:04x}:{args.pid:04x}"],
        check=False,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        raise RuntimeError(result.stderr.strip() or "matching USB descriptor not found")
    print(result.stdout, end="")


def run_loopback(args):
    started = time.monotonic()
    counts = {length: 0 for length in args.lengths}
    sequence = itertools.count()

    with UsbDevice(args.vid, args.pid, args.timeout_ms) as device:
        for length in itertools.cycle(args.lengths):
            index = next(sequence)
            if args.seconds and (time.monotonic() - started >= args.seconds):
                break
            if not args.seconds and index >= args.iterations:
                break
            expected = payload(index, length)
            actual = device.transfer(expected)
            if actual != expected:
                raise RuntimeError(
                    f"mismatch at transfer {index}, length {length}: got {len(actual)} bytes"
                )
            counts[length] += 1

    total = sum(counts.values())
    elapsed = time.monotonic() - started
    print(f"PASS transfers={total} elapsed={elapsed:.3f}s rate={total / max(elapsed, 0.001):.1f}/s")
    print("lengths " + " ".join(f"{length}:{counts[length]}" for length in args.lengths))


def self_test(_args):
    assert parse_lengths("0,1,0x40,1024") == (0, 1, 64, 1024)
    assert payload(255, 3) == b"\xff\x00\x01"
    assert [length for length in DEFAULT_LENGTHS if needs_out_zlp(length)] == [64, 512]
    print("PASS host self-test")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=DEFAULT_VID)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=DEFAULT_PID)
    parser.add_argument("--timeout-ms", type=int, default=1000)
    commands = parser.add_subparsers(dest="command", required=True)

    descriptors = commands.add_parser("descriptors")
    descriptors.set_defaults(action=show_descriptors)

    run = commands.add_parser("run")
    run.add_argument("--iterations", type=int, default=10000)
    run.add_argument("--seconds", type=float, default=0)
    run.add_argument("--lengths", type=parse_lengths, default=DEFAULT_LENGTHS)
    run.set_defaults(action=run_loopback)

    check = commands.add_parser("self-test")
    check.set_defaults(action=self_test)

    args = parser.parse_args()
    if getattr(args, "iterations", 1) < 1 or getattr(args, "seconds", 0) < 0:
        parser.error("iterations must be positive and seconds must not be negative")
    try:
        args.action(args)
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
