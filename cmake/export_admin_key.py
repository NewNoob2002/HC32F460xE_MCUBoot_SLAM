#!/usr/bin/env python3
"""Export an ECDSA-P256 administrator public key PEM as a raw X||Y C array for Boot.

Only public keys are accepted so the administrator private key never enters the firmware build.
"""
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: export_admin_key.py <public-key.pem> <output.c>", file=sys.stderr)
        return 2
    pem = Path(sys.argv[1]).read_bytes()
    if b"PRIVATE KEY" in pem:
        print("HC32_ADMIN_PUBLIC_KEY must be a public key; keep the private key out of the build", file=sys.stderr)
        return 1
    key = serialization.load_pem_public_key(pem)
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        print("HC32_ADMIN_PUBLIC_KEY must be an ECDSA-P256 public key", file=sys.stderr)
        return 1
    point = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    raw = point[1:]
    lines = [", ".join(f"0x{byte:02x}" for byte in raw[i : i + 8]) for i in range(0, len(raw), 8)]
    body = ",\n    ".join(lines)
    Path(sys.argv[2]).write_text(
        "/* Generated from HC32_ADMIN_PUBLIC_KEY by cmake/export_admin_key.py. Do not edit. */\n"
        "#include <stdint.h>\n\n"
        f"const uint8_t hc32_admin_public_key[64] = {{\n    {body},\n}};\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
