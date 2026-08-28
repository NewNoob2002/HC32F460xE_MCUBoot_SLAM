#!/usr/bin/env python3
import sys
from pathlib import Path


def require_once(image: bytes, expected: bytes, label: str) -> None:
    count = image.count(expected)
    if count != 1:
        raise SystemExit(f"{label}: expected once, found {count}")


def require_present(image: bytes, expected: bytes, label: str) -> None:
    if expected not in image:
        raise SystemExit(f"{label}: missing")


def device_descriptor(vid: int, pid: int) -> bytes:
    return bytes(
        [
            0x12, 0x01, 0x10, 0x02, 0, 0, 0, 0x40,
            vid & 0xFF, vid >> 8, pid & 0xFF, pid >> 8,
            0x01, 0x00, 0x01, 0x02, 0x03, 0x01,
        ]
    )


def verify(path: str, vid: int, pid: int, serial_prefix: str, mode: str) -> None:
    image = Path(path).read_bytes()
    require_once(image, device_descriptor(vid, pid), f"{mode} device descriptor")
    require_once(
        image,
        bytes.fromhex(
            "050f2100011c100500df60ddd88945c74c9cd2659d9e648a9f"
            "00000306a2002000"
        ),
        f"{mode} BOS WinUSB capability",
    )
    require_once(
        image,
        bytes.fromhex("0a00000000000306a2001400030057494e5553420000"),
        f"{mode} Microsoft OS 2.0 compatible ID",
    )
    require_once(
        image,
        "DeviceInterfaceGUIDs".encode("utf-16le") + b"\0\0",
        f"{mode} WinUSB interface GUID property",
    )
    require_present(image, serial_prefix.encode(), f"{mode} UQID serial prefix")
    if b"HC32F460-PHASE5-0001" in image:
        raise SystemExit(f"{mode}: lab-only serial remains in firmware")


if __name__ == "__main__":
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: verify_winusb_descriptors.py BOOT APP VID BOOT_PID APP_PID SERIAL_PREFIX"
        )
    verify(sys.argv[1], int(sys.argv[3]), int(sys.argv[4]), sys.argv[6], "Boot recovery")
    verify(sys.argv[2], int(sys.argv[3]), int(sys.argv[5]), sys.argv[6], "Application")
    print("Verified Boot/Application USB 2.1 + Microsoft OS 2.0 descriptors")
