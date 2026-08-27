# Phase 5 core USB upgrade HIL evidence

One v1.0.0 -> v2.0.0 -> confirmation -> independent reset -> persistence
cycle passed on 2026-08-27 using J-Link `20781318` and USB serial
`HC32F460-PHASE5-0001`. The target was left on confirmed v2.0.0.

The runtime C, Rust and CMake inputs are equivalent to source revision
`cfd87525f86ba22465784dba13c38e5de5d76759`; the HIL run itself occurred
before that dirty worktree was committed. This bundle is therefore immutable
core-HIL evidence, but it does not replace the clean-revision repeat required
to close G5.

`SHA256SUMS` covers every retained file except itself. The bundle intentionally
omits the v2 CMake/Ninja build tree and retains only executable artifacts,
structured results, safety preflights, logs, raw J-Link commands, Flash backup
and pre/post-reset snapshots.
