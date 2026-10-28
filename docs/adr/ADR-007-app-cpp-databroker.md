# ADR-007: Bounded C++ Application DataBroker

- Status: Accepted
- Date: 2026-10-28
- Scope: Product Application only

## Context

The C publish/subscribe Application at `8446d9b` already separates Power,
Charger, Update, System and Diagnostics. The GNSS_Panel reference provides a
C++ DataNode/DataBroker/DataTimer model, typed readers and injected device
contracts. The bare-metal target needs those abstractions without an allocator,
RTOS, exceptions or an implicit change to Boot or firmware-update trust boundaries.

## Decision

Use C++17 for `App`, DataProc, Diagnostics, device parsing and HAL adapters.
Port GNSS_Panel's MIT-licensed framework, retaining its license header and
synchronous event model. Use 16 caller-owned node slots, eight publisher and
subscriber links per node, and a DataTimer stored by value inside each node.
There is no automatic main-node subscription or separately allocated timer.

Node registration is explicit during startup and reports duplicate/empty IDs
and capacity errors. Failed registration preserves the existing object.
Nodes are non-copyable. Their destructors remove both sides of their links;
the broker must outlive all nodes. Base-class deletion is outside the ownership
contract; nodes and HAL adapters have no virtual deleting destructor. The App
holds all resources by value, with its broker declared before its nodes.

All dispatch runs synchronously on the cooperative main loop. Publish takes
an address/registration-serial snapshot in subscription order, skips removed
subscribers, stops on STOP_PROCESS and otherwise returns the last callback
error. A subscriber filtering out PUBLISH is not a delivery error. Pull and
notify require a subscription; typed readers check sender, event and exact
payload size and copy borrowed payloads. Callbacks must not retain pointers or
destroy the active publisher/broker. Timers use unsigned elapsed arithmetic,
carry `uint32_t now_ms`, dispatch at most once per pass, and defer nested
handlers by returning zero. No active timer means `UINT32_MAX` until next work.

`hal::I2cBus` and `hal::ChargerSignals` expose narrow virtual contracts with
caller-owned implementations. The HC32 adapters call the existing C BSP;
host tests supply fake objects rather than replacement BSP symbols. BQ40Z50
keeps ManufacturerAccess little-endian words and big-endian firmware fields.
Identity output is committed only after all four reads succeed.

Power initializes I2C2 and probes `0x0B`, `0x0C`, `0x08`, `0x5C` in order,
reading identity only at `0x0C` after ACK. Missing devices are degraded status.
Charger samples at init and every 100 ms and publishes changes; failed GPIO
init leaves valid=false and no timer. Update polls USB every loop pass and
publishes only configured/manager-state/error changes. System feeds the
watchdog every pass, counts loops and wraps image confirmation; neither uses
a node timer. Diagnostics subscribes to all providers, pulls every 5000 ms,
logs via CORE_DEBUG_PRINTF, and updates the unmangled `g_app_diagnostics`
snapshot. Duplicate diagnostics init cannot reset its live state or links.

`main.cpp` delegates to App. Startup order, panic strings, the Phase2 HIL path,
write-protection restore, deferred USB reset, LED poll and WFI match the C
entrypoint. Domain logic remains outside orchestration.

## Boundaries and enforcement

- Boot, components (including portable fw_update and all vendored code), shared
  BSP sources, `usb_fw_update.c`, `app_confirm.c` and `phase2_hil.c` remain C.
  Only project-owned C headers acquire linkage guards. Vendored headers are
  included with C linkage where needed and are not edited.
- App C++ compiles with `-fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-use-cxa-atexit` and `-Wall -Wextra -Wconversion -Wshadow -Werror`.
- The architecture check limits framework includes to its own headers and the
  C/C++ standard library, rejects direct HC32 LL/device includes from App,
  rejects DataProc dependencies on Diagnostics and forbids App heap operations.
- `verify_newlib_syscalls` also checks the linked App map for allocation
  operators, `__cxa_guard_*`, `__gxx_personality` and exception/RTTI support.
  Discarded input sections do not count as linked runtime. Host tests exercise
  the rejection patterns.
- Memory layout, signature verification, Secondary-only writes, rollback and
  immediate baseline image confirmation are unchanged.

## Consequences and validation

Typed state and HAL injection replace C globals and link-time device fakes.
Static object layout/vtables may increase Flash/data costs; no libstdc++
exception/RTTI or C++ allocation runtime is permitted. The shared C newlib
runtime remains outside that restriction. Constructors require no heap;
the existing startup's `__libc_init_array` remains the initialization mechanism.

Ported host suites preserve registration, ordering, errors, pull/notify,
wraparound, device identity, charger and diagnostics behavior. Added checks
cover nested timers, subscription mutation, duplicate initialization, failed
reads preserving state and direct USB/watchdog polling. Required build gates
are HostTests, Debug image/WinUSB/syscall checks, Phase2 HIL-mode compilation
and Release image/syscall checks (development key for compile proof only).

These checks do not establish physical I2C/GPIO, watchdog, USB enumeration,
reset, image swap/rollback or power-loss behavior. No flashing is part of this
rewrite; target HIL remains required before accepting the V1.1 image.
