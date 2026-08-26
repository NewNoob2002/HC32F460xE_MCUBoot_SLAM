#!/usr/bin/env python3
"""Verify the accepted Protocol V1 wire vectors using only stdlib."""

import csv
import struct
import zlib
from pathlib import Path

HEADER = struct.Struct("<4sBBBBIHH")
CRC = struct.Struct("<I")
MAX_PAYLOAD = 512
REQUIRED = {
    "hello_request",
    "hello_ok_response",
    "device_info_request",
    "device_info_ok_response",
    "begin_request",
    "begin_ok_response",
    "data_request",
    "data_ok_response",
    "end_request",
    "end_ok_response",
    "commit_request",
    "commit_ok_response",
    "abort_request",
    "abort_ok_response",
    "hello_incompatible_request",
    "hello_incompatible_response",
    "bad_sequence_response",
    "image_too_large_response",
    "storage_error_response",
}


def number(value: str) -> int:
    return int(value, 0)


def main() -> None:
    if zlib.crc32(b"123456789") != 0xCBF43926:
        raise SystemExit("CRC-32/ISO-HDLC reference check failed")

    path = Path(__file__).with_name("golden_vectors.csv")
    names = set()
    with path.open(newline="", encoding="ascii") as stream:
        for row in csv.DictReader(stream):
            name = row["name"]
            if name in names:
                raise SystemExit(f"duplicate vector: {name}")
            names.add(name)

            payload = bytes.fromhex(row["payload_hex"])
            if len(payload) > MAX_PAYLOAD:
                raise SystemExit(f"{name}: payload exceeds {MAX_PAYLOAD} bytes")

            header = HEADER.pack(
                b"FWUP",
                number(row["major"]),
                number(row["minor"]),
                number(row["command"]),
                number(row["flags"]),
                number(row["sequence"]),
                len(payload),
                0,
            )
            encoded = header + payload
            encoded += CRC.pack(zlib.crc32(encoded))
            expected = bytes.fromhex(row["frame_hex"])
            if encoded != expected:
                raise SystemExit(f"{name}: frame bytes do not match fields")

    missing = REQUIRED - names
    if missing:
        raise SystemExit(f"missing vectors: {', '.join(sorted(missing))}")
    print(f"PASS: {len(names)} Protocol V1 golden vectors verified")


if __name__ == "__main__":
    main()
