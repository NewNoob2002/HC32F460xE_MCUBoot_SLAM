# MCUboot Build Verification Report

Date: 2026-08-25

Branch: task8/final-verification-docs

Implementation baseline: ac8c03844d706937ec068357395e3bdf8bd9b676

## Result

Clean HostTests, Debug, and Release verification passed. The software evidence satisfies the Task8 build, layout, signing, and artifact checks. Physical rollback HIL has not yet been executed.

## Toolchain

| Tool | Version |
| --- | --- |
| CMake | 3.28.1 |
| Ninja | 1.11.1 |
| GNU Arm Embedded GCC | 14.3.1 (20250623) |
| Python | 3.12.3 |
| Host GCC | 13.3.0 |
| MCUboot | 2.4.0 |

## Host tests

The HostTests preset was freshly configured, clean-built, and run with CTest:

| Test | Result |
| --- | --- |
| memory_map | Passed |
| flash_map | Passed |
| boot_handover | Passed |
| app_confirm | Passed |

Result: 4/4 passed in 0.01 seconds.

## Firmware builds

Both presets used APP_VERSION=1.0.0 and APP_AUTO_CONFIRM=ON. Release reused an explicitly supplied ECDSA-P256 test key; build/Release/generated/keys/ contains only signing_keys.c, so no Release private key was generated.

| Preset | Image | text | data | bss | total |
| --- | --- | ---: | ---: | ---: | ---: |
| Debug | Boot | 24,268 | 100 | 2,436 | 26,804 B |
| Debug | App | 9,328 | 92 | 376 | 9,796 B |
| Release | Boot | 21,576 | 100 | 2,436 | 24,112 B |
| Release | App | 8,756 | 92 | 376 | 9,224 B |

Debug Boot occupies 26,804 bytes by the GNU size report and remains below its 65,536-byte partition limit. The generated Debug Boot binary is 24,752 bytes.

## Placement checks

| Check | Observed | Result |
| --- | --- | --- |
| Boot .icg_sec | address 0x00000400, size 0x20 | Passed |
| App .vectors | address 0x00010200, size 0x280 | Passed |
| App .icg_sec | absent | Passed |
| Primary padded image | 204,800 bytes | Passed |
| Update padded image | 204,800 bytes | Passed |

## Signed-image verification

verify_app_image validated app_signed.bin, app_primary.bin, and app_update.bin in both Debug and Release configurations.

- Debug image version: 1.0.0+0; payload digest: c097a45b42ffbf0215a8bae1658441d331504f0de3854f21e6342be4bda3ea4b.
- Release image version: 1.0.0+0; payload digest: 688e967db617efa439d7d421b4c474c9115d1772b6877fa5fec2a2372e834095.
- Primary trailer: boot magic set and image_ok=0x01.
- Update trailer: boot magic set and image_ok=0xFF, matching a test upgrade.

Debug artifact SHA-256 values:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| boot_firmware.bin | 24,752 | 72b2a457aef61d73c9ac1ea10531f70c5b00f5ddf34c8f6c03e8907c3cf5fbfd |
| app_signed.bin | 10,083 | 5899d9ae6557fa46ab311c1e2f75dd72120554a619a7cd9cd99da8cd1c80a797 |
| app_primary.bin | 204,800 | 4cb516988e1894d4a0563ec95c61771b527b044e38a5ce9819ba7bf89330f97c |
| app_update.bin | 204,800 | 79bb8f17646f10d05d061b5cf92f28ed613223afb1a0503e9cc115b897070954 |

Release artifact SHA-256 values:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| boot_firmware.bin | 22,060 | 719e535dc7514dc8098bef912e125bc300604130ab3d79938c0219e72553a756 |
| app_signed.bin | 9,512 | 0a8b93b6d87d790d5ebf6f84a5415b10b1ff99f64553b887caa571af646fbd03 |
| app_primary.bin | 204,800 | 1d93182a2e21753c27b01926e1c00f8334ea162cf5e510a281c853f6d8f5e4c5 |
| app_update.bin | 204,800 | 56a6a7044ab70a27de4ce969c3dff078c965b0218a830ee0261b5235e65f43d9 |

Signatures contain ECDSA randomness, so signed-image file hashes may change across otherwise identical rebuilds. imgtool verify, the embedded image digest, version, sizes, and trailer state are the reproducible acceptance checks.

## Remaining hardware verification

The required HIL sequence is: confirmed v1 Primary -> test v2 Secondary -> reset without confirmation and observe revert to v1 -> reinstall v2 -> confirm v2 -> reset and observe persistent v2. This must be executed on the HC32F460xE target with captured reset/boot evidence before claiming rollback is hardware-verified.
