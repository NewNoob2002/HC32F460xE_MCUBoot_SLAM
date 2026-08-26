# MCUboot Build Verification Report

Date: 2026-08-25

Branch: task8/final-verification-docs

Implementation baseline: ac8c03844d706937ec068357395e3bdf8bd9b676

HIL source revision: 339f32cc0145ecdc71878c68a8cb4af543be2920

Evidence freeze date: 2026-08-26

Evidence freeze base revision: a004c63bd34745bdbd23b7d1888e8153d32f0fab

## Result

Clean HostTests, Debug, Release, and physical rollback HIL verification passed. The evidence satisfies the Task8 build, layout, signing, artifact, upgrade, revert, and confirmation-persistence checks.

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

## Physical HIL verification

The rollback sequence was executed on 2026-08-25 using an HC32F460xE target, J-Link serial 20781318, SWD at 4 MHz, and VTref between 3.351 V and 3.356 V. The target identified as Cortex-M4 r0p1. Reserved Flash at 0x00076000-0x0007FFFF was not accessed; the previous contents of 0x00000000-0x00075FFF were backed up before the test.

| Stage | Primary after boot | Secondary after boot | App state | Result |
| --- | --- | --- | --- | --- |
| Confirmed v1 baseline | 1.0.0 | erased | PC in App, image_ok set | Passed |
| Unconfirmed v2 test boot | 2.0.0 | 2.0.0 during test state | PC in App, image_ok unset | Passed |
| Reset without confirming v2 | 1.0.0 | 2.0.0 | PC in App | Revert passed |
| Auto-confirming v2 boot | 2.0.0 | 2.0.0 during test state | confirmation returned 0, image_ok set | Passed |
| Reset after confirming v2 | 2.0.0 | 1.0.0 | PC in App, image_ok retained | Persistence passed |

The first 5-second sample caught execution inside TinyCrypt ECDSA verification. Subsequent boot gates used a 30-second bounded wait and observed the App at PC 0x00010730. The final target state is confirmed v2 running from Primary.

At test time, local evidence was stored under build/HIL/evidence/. The pre-test Flash backup was 483,328 bytes with SHA-256 60e089f7542b41156d24f1e65bbb14332574a7b1bd3118ea882bf47211f80f7f.

## HIL evidence retention decision

Phase 0A preserved the exact four programmed firmware images, raw J-Link logs, historical command files, normalized build/deployment/test manifests, and SHA-256 checksums under evidence/hil/2026-08-25-339f32c/. These files are independent of the ignored build tree and survive a fresh clone.

The original 483,328-byte pre-test Flash backup remains excluded because a full-device dump may contain device-specific or sensitive data. Its SHA-256 remains 60e089f7542b41156d24f1e65bbb14332574a7b1bd3118ea882bf47211f80f7f. Reusable path-independent J-Link templates are tracked under Tests/HIL/; the preserved historical commands retain their original absolute paths as evidence only.

The normal CI workflow verifies every retained SHA256SUMS file but does not execute hardware tests. Future release-qualifying HIL runs must create a new immutable evidence directory; a successful hardware run without retained evidence does not pass its release gate.
