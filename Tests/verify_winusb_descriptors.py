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


def verify(
    path: str,
    vid: int,
    pid: int,
    serial_prefix: str,
    manufacturer: str,
    product: str,
    mode: str,
) -> None:
    image = Path(path).read_bytes()
    require_once(image, device_descriptor(vid, pid), f"{mode} device descriptor")
    composite = mode == "Application"
    total = 338 if composite else 162
    bos = bytes.fromhex("050f2100011c100500df60ddd88945c74c9cd2659d9e648a9f00000306")
    require_once(image, bos + total.to_bytes(2, "little") + b"\x20\x00", f"{mode} BOS")
    header = bytes.fromhex("0a00000000000306") + total.to_bytes(2, "little")
    require_once(image, header, f"{mode} OS descriptor set header")
    offset = image.index(header)
    msos = image[offset:offset + total]
    compatible = bytes.fromhex("1400030057494e55534200000000000000000000")
    guid_name = "DeviceInterfaceGUIDs".encode("utf-16le") + b"\0\0"
    if composite:
        if msos[10:18] != bytes.fromhex("0800010000004801"):
            raise SystemExit(f"{mode}: invalid configuration subset")
        for interface in range(2):
            start = 18 + interface * 160
            subset = bytes.fromhex("08000200") + bytes([interface, 0]) + bytes.fromhex("a000")
            if msos[start:start + 8] != subset or msos[start + 8:start + 28] != compatible:
                raise SystemExit(f"{mode}: invalid function subset {interface}")
            require_once(msos[start:start + 160], guid_name, f"{mode} interface {interface} GUID")
    else:
        if msos[10:30] != compatible:
            raise SystemExit(f"{mode}: changed single-interface compatible ID")
        require_once(msos, guid_name, f"{mode} GUID")
    config = bytes.fromhex("0902") + (55 if composite else 32).to_bytes(2, "little")
    config += bytes([2 if composite else 1, 1, 0, 0x80, 50])
    config += bytes.fromhex("0904000002ff0000040705810240000007050202400000")
    if composite:
        config += bytes.fromhex("0904010002ff0000050705830240000007050402400000")
        require_once(image, b"Vendor Bulk Debug", f"{mode} debug string")
    elif b"Vendor Bulk Debug" in image:
        raise SystemExit("Boot recovery: debug interface must be absent")
    require_once(image, config, f"{mode} configuration/endpoints")
    require_present(image, serial_prefix.encode(), f"{mode} UQID serial prefix")
    require_once(image, manufacturer.encode(), f"{mode} manufacturer string")
    require_once(image, product.encode(), f"{mode} product string")
    if b"HC32F460-PHASE5-0001" in image:
        raise SystemExit(f"{mode}: lab-only serial remains in firmware")


if __name__ == "__main__":
    if len(sys.argv) != 10:
        raise SystemExit(
            "usage: verify_winusb_descriptors.py BOOT APP VID BOOT_PID APP_PID "
            "SERIAL_PREFIX MANUFACTURER BOOT_PRODUCT APP_PRODUCT"
        )
    verify(
        sys.argv[1],
        int(sys.argv[3]),
        int(sys.argv[4]),
        sys.argv[6],
        sys.argv[7],
        sys.argv[8],
        "Boot recovery",
    )
    verify(
        sys.argv[2],
        int(sys.argv[3]),
        int(sys.argv[5]),
        sys.argv[6],
        sys.argv[7],
        sys.argv[9],
        "Application",
    )
    print("Verified Boot/Application USB 2.1 + Microsoft OS 2.0 descriptors")
