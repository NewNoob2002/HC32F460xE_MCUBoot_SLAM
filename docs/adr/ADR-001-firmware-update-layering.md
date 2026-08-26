# ADR-001: Firmware Update Layering

Status: Accepted

Date: 2026-08-26

## Context

The repository has a verified HC32F460 MCUboot Boot/App baseline but no runtime firmware receive path. The product goal requires multiple MCUs, transports and protocols without duplicating the updater or coupling hardware callbacks to Flash/MCUboot state. MCUboot Primary and Secondary are slots for one Application product, not separate application implementations.

## Decision

- Boot owns trust, validation, image selection, swap, rollback and handover.
- The full FW Update Manager belongs to the Application image.
- Manager, Protocol, Transport, Storage, Boot Control and Platform are separate responsibilities.
- Protocol consumes transport-neutral chunks and never includes USB/UART/CAN/MCU headers.
- Transport never selects or writes MCUboot slots.
- Storage accepts logical image offsets and exposes only the Secondary candidate area.
- Boot Control separately requests a test upgrade and confirms the running image.
- Platform owns MCU-specific Flash, timer, reset, peripheral and ISR work. ISR code only transfers bounded data/events to Application context.
- A minimal Boot recovery profile, if justified by Phase 6 evidence, is a separate build/profile and does not reuse the full Application updater by default.
- Phase 1 documents semantics only. The first C contracts are introduced with their real Phase 2/3 implementation and host fake.

## Alternatives

### Full updater in Boot

Rejected as the default because it expands the most security-sensitive and stable image with transports, protocol/session state and product lifecycle policy.

### Separate updater for every transport

Rejected because USB/CAN/UART implementations would duplicate storage, validation, state and error handling and drift over time.

### Protocol calls MCUboot Flash APIs directly

Rejected because host-controlled offsets could escape the candidate slot and portability would be lost.

### One generic hardware abstraction with every possible operation

Rejected as speculative. Contracts are added only when one fake and one real implementation need them.

## Consequences

Positive:

- Boot remains small and stable.
- One Application updater source evolves across product versions.
- Transports and MCUs can be added without rewriting Protocol/manager logic.
- Secondary-only bounds are enforceable and host-testable.

Costs:

- Application images grow to include updater functionality.
- Cross-layer coordination requires explicit state/error contracts and more integration tests.
- Boot recovery is unavailable until separately justified and implemented.

Enforcement:

- Host-build portable targets.
- CI forbidden-include/dependency checks.
- Per-phase architecture diff review.
- G2/G3/G7/G8/G9 portability gates.
