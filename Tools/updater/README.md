# hc32-updater

Phase 5 host updater. The CLI and Slint GUI share the Rust library in this
package. USB uses the pinned blocking `nusb` 0.2.3 API; no Tokio runtime, plugin
system or transport registry is present.

```sh
cargo build --manifest-path Tools/updater/Cargo.toml --locked
Tools/updater/target/debug/hc32-updater info
Tools/updater/target/debug/hc32-updater config get
Tools/updater/target/debug/hc32-updater config set \
  --device-serial SN12AB34 --hardware-version A1.2 --app-pid 0x0020
Tools/updater/target/debug/hc32-updater install <signed-image>
Tools/updater/target/debug/hc32-updater wait --version 2.0.0
cargo run --manifest-path Tools/updater/Cargo.toml --bin hc32-updater-gui
```

The CLI currently uses one protocol session per process. If an immediate
follow-up command returns `BadSequence`, wait for the advertised 5000 ms session
timeout before retrying. The GUI keeps one serialized connection and is not
affected by this one-shot CLI limitation.

Builds read the project identity from `Config/Product/ProductIdentity.env`.
Before provisioning, firmware appends the HC32F460 96-bit UQID to `HC32F460-`.
After provisioning, Boot and Application use the configured device serial. An
external identity file can still override the public product identity, for
example:

```sh
HC32_PRODUCT_IDENTITY_FILE=/secure/product/ProductIdentity.env \
  cargo build --manifest-path Tools/updater/Cargo.toml --release --locked
```

`config get` reports the device serial, hardware version, Application PID and
whether they have been provisioned in FlashDB. `config set` is accepted only by
Boot recovery and is write-once. Device serial accepts 1-32 ASCII letters or
digits; hardware version accepts 1-16 ASCII letters, digits or dots. The
PID must be inside the approved range in the product identity file and cannot
equal the fixed Boot PID. The signed-image compatibility identity remains a
build-time constant and is not changed by provisioning.

Use `artifacts/app-signed-<version>.bin` with `install`. The slot-padded
`app-primary-<version>.bin` and `app-update-<version>.bin` are not protocol-transfer inputs.
Set the Application/MCUboot image version when configuring the firmware build,
for example `cmake --preset Debug -DAPP_VERSION=2.0.0 -DAPP_AUTO_CONFIRM=ON`.
The accepted form is `MAJOR.MINOR.REVISION[+BUILD]`.

Boot recovery always enumerates as `cafe:0001`; the Application updater defaults
to `cafe:0002` and loads a provisioned product PID before USB registration. Both
expose interface 0 through Microsoft OS 2.0 descriptors for automatic WinUSB
binding. Discovery matches VID, updater interface description and a reported serial,
then confirms the protocol only on Connect. `install` requires one valid protected
compatibility TLV and provisioned device parameters, rejecting either failure before
BEGIN/erase. Both modes write only MCUboot Secondary; run recovery tests only
after the target/backup/restore safety preflight.

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

The GUI opens on the `Firmware Update` tab; write-once manufacturing fields are
kept on the secondary `Device Setup` tab. It has one USB worker. Refresh only
updates the device list; select a row and Connect to claim interface 0 and
complete HELLO plus DeviceInfo. While idle, the worker sends DeviceInfo every
2000 ms without repeating HELLO. Configuration
and install run on the same worker, so sequence numbers cannot race. After
COMMIT, the GUI waits for the same serial to return in Application mode and
keeps the reconnected client. The content remains mouse-wheel scrollable on
small or high-DPI displays.

## Application Debug Link

Debug Link V1 uses Application interface 1 (`Vendor Bulk Debug`, OUT `0x04`,
IN `0x83`), independently of the existing interface 0 updater. Boot Recovery
does not expose Debug. Select the Application by its exact USB serial:

```sh
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 info
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 monitor \
  --period 500 --log --csv debug.csv --duration 30
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 config list
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 config get charge_enable
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 config set charge_enable 1 --yes
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 config reset
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 reg-read 0 0x10 4
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 stats
Tools/updater/target/debug/hc32-updater debug --serial SN12AB34 reboot
```

Keys accept CONFIG_LIST names, decimal IDs or `0x` hexadecimal IDs. Values
accept signed decimal or hexadecimal integers; boolean values are `0`/`1`.
Set, reset and reboot ask for confirmation unless `--yes` is supplied, then
UNLOCK with the opened device's actual USB serial. These three commands are
never automatically resent: a timeout leaves their outcome unknown. Only
idempotent requests retry timeout/interrupted I/O, with at most three sends.

Monitor defaults to 1000 ms; valid periods are 100–5000 ms. Duration is a
positive integer number of seconds; omitting it streams continuously. CSV
contains one row per telemetry field or log event, with host elapsed time,
event sequence and log metadata; quoted fields preserve commas and newlines.
Normal completion and returned errors attempt to stop streaming. Abrupt process
termination cannot send that request; another HELLO or USB reset stops the device
stream. The client queues up to 256 events that arrive before responses, dropping
the oldest on overflow and exposing `events_dropped` to library callers.

In the GUI, connect an Application device and select **Debug → Open Debug**.
The page shows live telemetry and the latest 200 log entries. Set the period,
toggle logs, and use Start/Stop to control streaming. Refresh reloads telemetry,
settings and statistics. Edit and Apply one setting at a time, or reset all
settings; both operations require the confirmation dialog and serial UNLOCK.
Debug uses the existing background worker, so USB requests never block the UI.
Disconnect, reconnect, installation and connection failure close the Debug
session; reopen it after reconnecting.

The host decodes BQ identity using the contract's explicit 17-byte layout and
retains unknown TLV values as bytes (displayed as hex). The wire contract leaves
log text encoding unspecified; the host displays UTF-8 with replacement for invalid
sequences. Per-setting Apply and explicit statistics refresh are the GUI choices.

Host verification uses scripted transports and software-rendered GUI snapshots;
real interface claiming, USB framing, streaming and device mutations still require
hardware validation. No new dependencies are required.
