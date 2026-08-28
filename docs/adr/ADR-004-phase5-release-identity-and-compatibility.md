# ADR-004: Phase 5 Release Identity and Signed Compatibility

Status: Accepted

Date: 2026-08-28

## Context

Protocol V1 currently copies the connected device's hardware and board values
into BEGIN. That is useful as an early mistake check, but it does not prove that
the signed image itself targets that device. Phase 5 also still uses the lab-only
USB identity, has no automatic Windows WinUSB binding, and has no frozen
Windows executable-signing input contract. These decisions must be stable
before final GUI HIL and portable-release evidence are meaningful.

## Decision

### Signed image compatibility

- Image compatibility is carried in one MCUboot custom protected TLV with type
  `0x00A0`. MCUboot 2.4.0 places custom TLVs in the protected area, so the image
  signature covers the value.
- The V1 payload is exactly 12 little-endian bytes:

| Offset | Size | Field | Rule |
| ---: | ---: | --- | --- |
| 0 | 1 | format version | Must be `1` |
| 1 | 1 | reserved | Must be zero |
| 2 | 2 | board revision | Exact match |
| 4 | 4 | hardware ID | Exact match |
| 8 | 4 | board ID | Exact match |

- Release images must contain exactly one valid compatibility TLV. Missing,
  duplicate, malformed or mismatched metadata fails closed. Revision ranges and
  compatibility sets are deferred until a second real board revision requires
  them.
- The Host parses the TLV and rejects incompatibility before BEGIN/erase. The
  device must independently validate the candidate with MCUboot and re-read the
  same protected TLV before COMMIT. Host declarations remain only an early
  transport check.
- `imgtool --custom-tlv 0x00A0 0x<24-hex-digits>` is the signing input. The
  generated value comes from the same repository-owned product identity used by
  firmware and Host builds; it is never supplied interactively by the updater.

### USB identity

- The project-owner supplied state identities are `cafe:0001` for Boot recovery
  and `cafe:0002` for the Application updater. Firmware appends the HC32F460
  96-bit UQID to `HC32F460-`, producing one stable per-chip serial in both modes.
- The VID, both PIDs, manufacturer, products and unique serial format are public
  build inputs. One generated identity must feed the firmware descriptor, Rust
  CLI and Rust GUI; independent hard-coded copies are rejected.
- Release configuration fails when the project-owner identity is absent or
  marked non-production. The repository does not synthesize an identity during
  a Release build.

### Windows WinUSB binding

- Supported Windows executables use the operating-system WinUSB driver through a
  Microsoft OS 2.0 descriptor set and BOS platform capability for updater
  interface 0.
- No custom kernel driver, Zadig step or manual INF installation is accepted.
  The descriptor identity must match the production VID/PID used by the tools.

### Windows executable signing

- The code-signing certificate, private key/password and timestamp URL are
  external release inputs. They are never committed or copied into retained
  evidence.
- The release job signs both CLI and GUI EXEs with SHA-256 and an RFC3161
  timestamp, verifies both outputs, and then creates a portable ZIP containing
  only the two EXEs, README and SHA256SUMS.
- Evidence retains executable/archive hashes, signer identity, timestamp and
  verification output. Missing credentials may build an unsigned developer ZIP,
  but cannot pass the signed-Windows portion of G5.

## Alternatives

### Trust Host-declared hardware and board IDs

Rejected because a modified or incorrect Host can claim that any image matches
the connected device. The declaration is not covered by the image signature.

### Use an unsigned sidecar manifest

Rejected because it can be replaced independently of the signed firmware.

### Keep separate VID/PID constants in firmware, CLI and GUI

Rejected because the three copies can drift and make packaged tools unable to
discover their firmware.

### Ship a custom Windows driver

Rejected because the updater needs only vendor Bulk transfers and WinUSB already
provides the required user-space interface.

## Consequences

- Phase 5A contract decisions, compatibility, USB identity and UQID serial are
  implemented. G5 remains blocked on externally signed Windows EXEs and clean
  Windows direct-run evidence.
- Final GUI HIL must run after those implementation changes; the existing CLI
  HIL remains valid evidence for the lab identity and current core path.
- UART/CAN, downgrade policy and security counters remain separate later work.

## Acceptance condition

Accepted when Host and device reviews use this ADR as the single Phase 5 rule
for signed compatibility, production USB identity, automatic WinUSB binding and
Windows signing inputs.
