# hc32-updater

Phase 5 host updater. The CLI and Slint GUI share the Rust library in this
package. USB uses the pinned blocking `nusb` 0.2.3 API; no Tokio runtime, plugin
system or transport registry is present.

```sh
cargo build --manifest-path Tools/updater/Cargo.toml --release --locked
Tools/updater/target/release/hc32-updater info
Tools/updater/target/release/hc32-updater install <signed-image>
Tools/updater/target/release/hc32-updater wait --version 2.0.0
cargo run --manifest-path Tools/updater/Cargo.toml --bin hc32-updater-gui
```

Use `artifacts/updater_signed.bin` with `install`. The slot-padded
`updater_primary.bin` and `updater_update.bin` are not protocol-transfer inputs.

The current `fffe:ffff` VID/PID is for Phase 5 lab validation only. `install`
erases and writes the MCUboot Secondary Slot through the device Manager; run it
only after the HIL target/backup/restore safety preflight.

The GUI keeps blocking USB work on a worker thread and reports progress back to
Slint's main event loop. Enter the signed image path, refresh the device, then
install. A native file picker is deferred until the path-input workflow proves
insufficient.
