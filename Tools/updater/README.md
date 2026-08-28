# hc32-updater

Phase 5 host updater. The CLI and Slint GUI share the Rust library in this
package. USB uses the pinned blocking `nusb` 0.2.3 API; no Tokio runtime, plugin
system or transport registry is present.

```sh
cargo build --manifest-path Tools/updater/Cargo.toml --locked
Tools/updater/target/debug/hc32-updater info
Tools/updater/target/debug/hc32-updater config get
Tools/updater/target/debug/hc32-updater config set \
  --hardware-id 0x00004600 --board-id 1 --board-revision 2
Tools/updater/target/debug/hc32-updater install <signed-image>
Tools/updater/target/debug/hc32-updater wait --version 2.0.0
cargo run --manifest-path Tools/updater/Cargo.toml --bin hc32-updater-gui
```

Builds read the project identity from `Config/ProductIdentity.env`. Firmware
appends the HC32F460 96-bit UQID to `HC32F460-`, so Boot and Application expose
one stable per-chip serial. An external identity file can still override the
public product identity, for example:

```sh
HC32_PRODUCT_IDENTITY_FILE=/secure/product/ProductIdentity.env \
  cargo build --manifest-path Tools/updater/Cargo.toml --release --locked
```

`config get` reports the effective `hardware_id`, `board_id`,
`board_revision` and whether they have been provisioned in FlashDB. `config set`
is accepted only by Boot recovery and is write-once. Before the
first write, the build-time product identity is reported as an unprovisioned
default. The UQID-derived USB serial is never writable.

Use `artifacts/updater_signed.bin` with `install`. The slot-padded
`updater_primary.bin` and `updater_update.bin` are not protocol-transfer inputs.

Boot recovery enumerates as `cafe:0001`; the Application updater enumerates as
`cafe:0002`. Both expose interface 0 through Microsoft OS 2.0 descriptors for
automatic WinUSB binding. CLI/GUI accept either mode, while post-install `wait`
accepts only the Application PID. `install` requires one valid protected
compatibility TLV and rejects a mismatch before BEGIN/erase. Both modes write
only MCUboot Secondary; run recovery tests only after the target/backup/restore
safety preflight.

On Linux, install the included udev rule once so both USB modes are accessible
without manually changing permissions after each re-enumeration:

```sh
sudo install -Dm0644 Tools/updater/packaging/linux/70-hc32-updater.rules \
  /etc/udev/rules.d/70-hc32-updater.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=cafe
```

Remove it with `sudo rm /etc/udev/rules.d/70-hc32-updater.rules`, followed by
the same reload command.

Linux development and validation use the Release binaries directly; no package
installation is required. Cross-build the two Windows x64 executables, then
create the portable ZIP:

```sh
cargo build --manifest-path Tools/updater/Cargo.toml --release --locked \
  --target x86_64-pc-windows-gnu
Tools/updater/packaging/windows/build-portable-zip.sh
```

The Windows executable-signing inputs are external and mandatory for a release artifact. The
password is read from a file and is not placed on the command line:

```sh
HC32_WINDOWS_SIGN_PFX=/secure/windows-signing.pfx \
HC32_WINDOWS_SIGN_PASSWORD_FILE=/secure/windows-signing.password \
HC32_WINDOWS_TIMESTAMP_URL=https://trusted-rfc3161.example \
Tools/updater/packaging/windows/sign-executables.sh \
  Tools/updater/target/x86_64-pc-windows-gnu/release/hc32-updater.exe \
  Tools/updater/target/x86_64-pc-windows-gnu/release/hc32-updater-gui.exe \
  Tools/updater/target/x86_64-pc-windows-gnu/release/signed
```

`sign-executables.sh` signs both executables with SHA-256, requires an RFC3161
timestamp and verifies both results. Point `HC32_WINDOWS_CLI` and
`HC32_WINDOWS_GUI` at those signed files before building the release ZIP.
`HC32_WINDOWS_SIGN_CA_FILE` may point at a private release CA bundle.

The GUI keeps blocking USB work on a worker thread and reports progress back to
Slint's main event loop. Use Browse or enter the signed image path, refresh the
device, then install. The content remains mouse-wheel scrollable on small or
high-DPI displays.
