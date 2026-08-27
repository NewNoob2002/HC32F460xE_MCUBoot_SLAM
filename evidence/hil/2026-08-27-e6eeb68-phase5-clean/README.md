# Phase 5 clean-revision USB upgrade HIL evidence

Source revision `e6eeb68700662ef87f8093f13d4f3fac53dbe722` was clean
before the build and throughout the hardware run. On 2026-08-27 the board
completed one v1.0.0 -> v2.0.0 -> confirmation -> independent reset ->
persistence cycle using J-Link `20781318` and USB serial
`HC32F460-PHASE5-0001`. The target remains on confirmed v2.0.0.

The bundle retains exact host/firmware artifacts, the generated public-key
source, the pre-run Flash backup, every safety preflight, raw J-Link commands,
USB/J-Link logs and pre/post-reset snapshots. The ephemeral private signing key
is intentionally not retained. `SHA256SUMS` covers every retained file except
itself.

One preliminary direct `cargo test --features fake-e2e` invocation lacked the
CTest-provided `HC32_FAKE_DEVICE` fixture and is retained in `logs/00_rust_gate.txt`.
The intended strict HostTests entry passed 12/12, including the same Rust/C fake
E2E, and the corrected standalone Rust suite passed 10/10.
