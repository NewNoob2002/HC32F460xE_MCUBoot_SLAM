from pathlib import Path

root = Path(__file__).resolve().parent
flash = (root / "snapshots/post_install_00000000_0007ffff.bin").read_bytes()
backup = (root / "backup/pre_hil_a_00000000_0007ffff.bin").read_bytes()
restored = (root / "snapshots/post_restore_00000000_0007ffff.bin").read_bytes()
image = (root / "artifacts/updater_signed.bin").read_bytes()
primary = flash[0x10000:0x42000]
secondary = flash[0x42000:0x74000]
scratch = flash[0x74000:0x76000]
reserved = flash[0x76000:0x80000]
mcuboot_magic = bytes.fromhex("77c295f360d2ef7f3552500f2cb67980")
product_offsets = [0x76000 + i for i in range(len(reserved)) if reserved.startswith(b"HCPI", i)]

checks = {
    "primary_image_exact_match": primary[: len(image)] == image,
    "primary_header_valid": primary[:4] == bytes.fromhex("3db8f396"),
    "primary_copy_done": flash[0x41FE0] == 1,
    "primary_image_ok": flash[0x41FE8] == 1,
    "primary_magic_valid": flash[0x41FF0:0x42000] == mcuboot_magic,
    "secondary_header_erased": all(value == 0xFF for value in secondary[:512]),
    "secondary_trailer_erased": all(value == 0xFF for value in secondary[-256:]),
    "product_identity_present": bool(product_offsets),
    "reserved_changed_from_backup": reserved != backup[0x76000:0x80000],
    "post_restore_exact_match": restored == backup,
}

for name, passed in checks.items():
    print(f"{name}={str(passed).lower()}")
print("product_identity_offsets=" + ",".join(f"0x{offset:08x}" for offset in product_offsets))
print(f"secondary_non_erased_bytes={sum(value != 0xFF for value in secondary)}")
print(f"scratch_non_erased_bytes={sum(value != 0xFF for value in scratch)}")
assert all(checks.values())
