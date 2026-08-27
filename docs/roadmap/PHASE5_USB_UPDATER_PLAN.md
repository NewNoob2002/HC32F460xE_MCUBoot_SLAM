# Phase 5 USB Updater Plan

Status: Phase 5B/5C complete; Phase 5D clean-revision HIL passed with immutable evidence; Phase 5E in progress; G5 not passed

Date: 2026-08-27

Baseline: `ebad62c3b5bcf53c07fcbe01ca0b867a40c8e003` (G4 PASSED)

Phase 5 core implementation node: `cfd87525f86ba22465784dba13c38e5de5d76759`

Phase 5D clean HIL node: `e6eeb68700662ef87f8093f13d4f3fac53dbe722`

## Frozen decisions

- `Tools/host/usb_loopback.py` remains the Phase 4 USB regression tool. It is
  not extended into the firmware updater.
- The Phase 5 host updater is Rust and initially exposes only `info`, `install`
  and `wait`.
- USB uses `nusb` blocking operations (`MaybeFuture::wait()` and blocking Bulk
  transfers). Tokio and another async runtime are excluded.
- One Rust protocol/client core drives both fake E2E and real USB HIL.
- The core v1 -> v2 -> health confirmation -> reset persistence path must pass
  before Slint is added.
- The GUI is one window over the same updater core. It does not implement a
  second protocol or update path.
- Windows ships a signed `.msi`. Linux ships one installable package with
  the matching udev rule.
- Plugins, a transport registry, UART/CAN, download resume and Boot recovery
  are not Phase 5 work.

## Objective

Deliver the smallest releasable USB updater that installs a compatible signed
image through the existing Protocol V1 and Application Manager, boots it as an
MCUboot test upgrade, confirms it only after bounded Application health checks,
and proves that the confirmed image persists across another reset.

## Minimal architecture

```text
CLI: info / install / wait       Slint single window (after core HIL)
              \                 /
               hc32-updater library
        FirmwareImage + ProtocolV1Client + UpgradeWorkflow
                  progress events
                   /                         \
      test-only stdio link                 blocking nusb link
                |                                  |
 existing C Manager + fake backends     Vendor Bulk -> C Manager
                                                   |
                                  Secondary Storage + Boot Control
```

The shared Rust library owns MCUboot image-header/version inspection, Protocol
V1 encoding/decoding, request sequence, bounded retry, image CRC/chunking,
compatibility decisions and workflow progress events. Links only move bytes and
report timeout/disconnect. CLI and GUI are sibling frontends. There is no runtime
registry, plugin API or transport-selection CLI flag in Phase 5.

`FirmwareImage` is intentionally not a general package abstraction. Phase 5
accepts one MCUboot signed-image file. Rust may reject malformed metadata and
compute transfer CRC, but MCUboot remains the signature trust root unless the
host later performs a complete trusted-public-key verification.

The fake E2E peer reuses the production C Manager with fake Storage and Boot
Control behind a small test-only stdin/stdout adapter. This avoids maintaining
a second device-state-machine implementation in Rust.

## Local implementation checkpoint

- Phase 5B is complete locally: the shared Rust core, `info`/`install`/`wait`
  CLI and fake E2E against the production C Manager are implemented.
- Phase 5C code is complete locally: blocking nusb discovery/claim/Bulk I/O and
  the production `usb_fw_updater` Application target feed the existing Manager,
  drain responses and execute its deferred RESET action.
- Rust tests are 11/11 and strict ASan/UBSan HostTests are 12/12. Debug and
  Release App, Phase 4 loopback and updater image builds/verifications pass.
- The initial read-only HIL attempt found the target Flash blank and no updater
  USB device. After a separately approved deployment preflight, Boot and the v1
  updater Primary were programmed and verified with J-Link `20781318`.
- Physical `info` passed, then one complete v1.0.0 -> v2.0.0 -> confirmation ->
  independent reset -> persistence cycle passed. USB remove/add events, zero
  confirmation/init/error results and byte-identical pre/post-reset headers and
  trailers are archived under
  `evidence/hil/2026-08-27-cfd8752-phase5-core/`.
- A second run from clean revision `e6eeb68` passed the same v1.0.0 -> v2.0.0
  -> confirmation -> independent reset -> persistence path. Exact artifacts,
  preflights, logs, backup and byte-identical pre/post-reset snapshots are under
  `evidence/hil/2026-08-27-e6eeb68-phase5-clean/`.
- The target remains on confirmed v2.0.0. Phase 5D's clean-repeat and immutable
  evidence requirement is satisfied; Phase 5E Slint work may begin. G5 remains
  open for GUI and package evidence.

## Initial file boundary

Phase 5B starts with only the files needed for the CLI and fake E2E:

- `Tools/updater/Cargo.toml`
- `Tools/updater/src/lib.rs` for the shared client core
- `Tools/updater/src/main.rs` for `info`, `install` and `wait`
- one test-only fake-device adapter backed by the existing C Manager

The nusb link is added in Phase 5C. It owns discovery/open/claim, Bulk I/O and
USB error mapping; the workflow owns bounded re-enumeration policy. Control
transfers and event-driven hotplug are not added without a protocol need. Slint
and packaging files are not
scaffolded until their preceding gates pass. Exact module splitting is deferred
until a source file becomes materially clearer by being split.

## CLI contract

```text
hc32-updater info
hc32-updater install <signed-image>
hc32-updater wait --version <version> [--timeout <seconds>]
```

- `info` discovers exactly one supported device, performs HELLO and DEVICE_INFO,
  and prints protocol capability, identity, version and slot capacity. Zero or
  multiple matching devices fail clearly. Device selectors are added only when
  a real multi-device requirement exists.
- `install` always means the only supported V1 policy: compatible signed image,
  test upgrade and reset after the COMMIT response is physically drained. There
  are no redundant `--test`, `--reboot` or `--transport usb` options.
- For this repository, the CLI input is `artifacts/updater_signed.bin`; the
  204800-byte `updater_primary.bin` and `updater_update.bin` are slot-padded
  direct-programming artifacts and exceed the protocol's logical image capacity.
- `wait` handles disappearance/re-enumeration and succeeds when the requested
  Application version answers DEVICE_INFO before the bounded timeout.
- Protocol V1 does not currently expose MCUboot confirmation state. G5 proves
  confirmation by a bounded Application confirmation result plus a second reset
  after which `wait` still finds v2 and retained slot/trailer evidence shows the
  image is permanent. Phase 5 does not add a status command only for this check.

## Execution phases

### Phase 5A — Contract and release preflight

1. Accept this plan and synchronize roadmap/gate/test documents.
2. Pin the reviewed `nusb` release when Rust code is first added. The reviewed
   planning reference is `nusb` v0.2.3, which documents blocking device listing,
   interface claim and Bulk transfer APIs.
3. Freeze the signed image compatibility source used for hardware/board/version
   checks before BEGIN. Host-supplied IDs remain an early filter, not image trust.
4. Freeze production USB VID/PID and Windows WinUSB binding. The current
   `fffe:ffff` values are test identifiers and cannot qualify a public package.
5. Define signing-secret injection outside the repository for Windows artifacts.

Exit: review approval. No Rust, GUI, package or hardware state change is part of
5A.

### Phase 5B — Rust core and fake E2E

1. Add the minimal Rust package and three-command CLI parsing.
2. Implement the transport-neutral Rust client core against
   `docs/protocol/PROTOCOL_V1.md` and existing Golden Vectors.
3. Add the test-only stdio adapter around the real C Manager and fake backends.
4. Run complete HELLO -> DEVICE_INFO -> BEGIN -> DATA -> END -> COMMIT -> RESET
   fake sessions, including chunk boundaries, duplicate response retry, timeout,
   disconnect and compatibility rejection.
5. Prove fake Storage contains exact image bytes and Boot Control is called once.

Exit: Rust format/clippy/tests pass and fake E2E uses the same client core that
will be linked to nusb.

### Phase 5C — Application USB binding and blocking nusb HIL

1. Replace Phase 4 echo behavior in a new production updater firmware target;
   keep the loopback target and Python tool unchanged for G4 regression.
2. Feed bounded Bulk OUT chunks into `fw_update_manager_feed()`, drain Manager TX
   to Bulk IN, report disconnect/TX-idle, poll timeouts and execute RESET only
   after `fw_update_manager_take_action()` emits it.
3. Add the blocking nusb link using the existing Vendor Bulk interface and
   endpoints. Use bounded transfer timeouts and explicit disconnect/stall errors.
4. Run `info` and a non-destructive protocol session on the physical target
   before permitting an image install.

Exit: the real USB path changes no Protocol, Storage or Boot-Control ownership
and Phase 4 loopback regression remains green.

### Phase 5D — Upgrade, confirmation and persistence HIL

Status: passed on clean revision `e6eeb68`; immutable evidence retained under
`evidence/hil/2026-08-27-e6eeb68-phase5-clean/`.

1. Prepare exact signed v1/v2 artifacts and a full safety preflight/restore path.
2. Establish confirmed v1, then run `info`, `install` and `wait --version 2.0.0`.
3. Confirm v2 only after bounded essential Application initialization succeeds.
4. Reset once more without writing Flash from the debugger and run `wait` again.
5. Capture read-only slot/trailer state, versions, logs and hashes, then restore
   the original target image byte-for-byte.

Exit: v1 -> v2 -> confirm -> persistence passes repeatedly with immutable
evidence. No debugger writes Secondary during the upgrade.

### Phase 5E — Slint single-window GUI

Status: local implementation/build complete; launch and physical install
evidence pending.

Only after Phase 5D passes:

- add one window containing device summary, image selection, install action,
  progress and final result;
- call the same Rust updater core used by the CLI;
- keep blocking USB work off the UI event loop with one standard-library worker
  thread;
- forward structured bounded progress events to Slint's main event loop;
- omit settings pages, plugin UI, transport selection, logs database, background
  service and automatic update checks.

Exit: GUI smoke test and one physical install produce the same protocol trace and
result as the CLI.

### Phase 5F — Release packages

- Windows: signed `.msi` containing the CLI and GUI; clean-VM install, launch,
  USB access, signature verification and uninstall must pass without Zadig or a
  manual driver step. Prefer automatic WinUSB binding over shipping a custom
  driver package.
- Linux: one Debian-family package containing the CLI, GUI and a rule under
  `/usr/lib/udev/rules.d/` for the frozen production VID/PID. Clean-VM install,
  non-root USB access, upgrade, removal and udev cleanup must pass. Other package
  formats wait for an actual distribution requirement.
- Private signing keys and certificates remain outside source and release
  evidence. Evidence records signer identity, verification output and hashes.

Exit: both packages install and operate on clean supported systems. Test VID/PID
or unsigned Windows artifacts cannot pass.

### Phase 5G — Gate closure

Run the full G5 matrix, retain Host/fake, USB HIL, GUI and package evidence, pass
remote CI, then update `STATUS.md` to `PASSED`.

## Required checks

### Automated

- Rust format, clippy with warnings denied, locked build and tests.
- Existing Protocol Golden Vectors consumed by the Rust core.
- Fake E2E happy path and bounded failures.
- Existing strict HostTests, portable dependency checks and Debug/Release image
  verification.
- Phase 4 Python loopback self-test and firmware build remain regression checks.
- Package contents, signatures and udev-rule identity are mechanically inspected.

### HIL

- Rust `info` against v1 and v2.
- Full USB install with no debugger Secondary write.
- Re-enumeration after COMMIT reset.
- Health confirmation followed by another reset and v2 persistence.
- CLI and GUI each complete the same core path at least once.
- Clean Windows and Linux package install/access/uninstall.

## Evidence

Retain under `evidence/host/` and `evidence/hil/`:

- source revision, dirty state, Rust/toolchain/dependency versions;
- exact v1/v2 images and SHA-256 hashes;
- fake E2E transcript and protocol trace;
- CLI/GUI transcript, raw USB/target logs and timing;
- confirmation result, post-reset version and slot/trailer state;
- target/probe identity, safety preflight and byte-exact restoration result;
- Windows/Linux package hashes, signer verification, contents and clean-VM results;
- `SHA256SUMS` and an immutable evidence index.

## Hard failures

G5 fails on any protocol fork between fake/USB/GUI, direct USB-to-Flash or
MCUboot call, debugger Secondary programming, unbounded retry/wait, confirmation
without a subsequent persistence reset, regression of the Phase 4 loopback,
manual Windows driver setup, missing Linux udev rule, test VID/PID in a public
package, unsigned Windows installer or incomplete evidence.

## Explicitly deferred

- Tokio or another async runtime.
- Plugin system, dynamic loading, transport registry/factory and configuration
  for transports that do not exist.
- UART, CAN/CAN FD and a second MCU.
- Download resume, persistent session metadata and recovery download.
- Boot recovery profile, permanent-upgrade command and anti-rollback counters.
- Multiple GUI windows, background service and self-update.
