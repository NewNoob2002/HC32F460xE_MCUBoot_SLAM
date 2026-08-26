# Phase 3 Protocol Core Plan

Status: Approved; Phase 3D complete, Phase 3E in progress

Date: 2026-08-26

Baseline: 9d29ee9843c711d7f853d7c24a7df58be8e8e1e9 (G2 PASSED)

Decision inputs:

- docs/adr/ADR-001-firmware-update-layering.md
- docs/adr/ADR-003-protocol-v1-boundary.md (Accepted)
- docs/protocol/PROTOCOL_V1.md (Accepted for Phase 3 implementation)

## Objective

Implement and prove a bounded, portable Protocol V1 parser plus firmware-update
session Manager using host fakes only. G3 proves framing, command/state logic,
logical image handling and error behavior before any real USB/UART/CAN code is
allowed.

## Preconditions

- G2 is PASSED with retained Storage/Boot-Control HIL evidence.
- ADR-003 and Protocol V1 wire values are reviewed and accepted.
- Maximum payload, command/status IDs, timeout/error limit and final-write
  padding policy are frozen before production headers are added.
- Existing Debug/Release/signing checks remain green.

## Audited starting point

Implemented:

- portable Storage and Boot-Control contracts;
- MCUboot Secondary and Boot-Control backends;
- logical capacity 0x30000 excluding the final trailer sector;
- aligned Storage writes, bounded reads and backend-controlled full-slot erase;
- HostTests 7/7, dependency check, Debug/Release builds and G2 HIL.

Not implemented:

- protocol header, codec or parser;
- update session Manager;
- Transport production API/backend;
- image receive/readback lifecycle;
- protocol corpus or golden vectors.

Critical integration fact:

The runtime artifact is unpadded app_signed.bin and may not end on the Storage
write alignment. Protocol DATA is not allowed to inherit Flash alignment.
Manager coalesces arbitrary DATA bytes in one bounded work buffer, writes only
aligned prefixes, and pads only the final physical write with erased_value;
bounds and CRC use the exact logical image size.

## Scope

- Protocol V1 constants, wire specification and golden vectors.
- CRC-32/ISO-HDLC and fixed-buffer encode/decode/parser.
- HELLO, DEVICE_INFO, BEGIN, DATA, END, COMMIT and ABORT.
- Common response statuses; no separate ACK/NACK command hierarchy.
- Manager state, sequence, duplicate replay and inactivity timeout.
- Strict contiguous receive, alignment staging/tail padding and full-image
  readback CRC.
- Boot-Control test-pending only after successful END.
- RESET action only after COMMIT response consumption and TX idle.
- Test-only fake byte driver, fake Storage and fake Boot Control.
- Deterministic malformed corpus and immutable G3 host evidence.

## Out of scope

- CherryUSB, USB, UART, CAN/CAN FD and host updater implementation.
- Production Transport registry, factory or plugin framework.
- Protocol fragmentation, windowing, multiple in-flight requests or resume.
- Compression, encryption, authentication or key exchange.
- MCUmgr/SMP, XMODEM/YMODEM or UDS implementation.
- Application-side MCUboot signature verification.
- Permanent upgrade, downgrade enforcement, security counters or Boot recovery.
- Product reset execution, real Application integration and HIL.

## Architecture impact

~~~text
Test-only fake byte driver
        |
        v
Manager RX feed / TX drain boundary
        |
        +--> Protocol V1 codec + incremental parser
        +--> Storage contract --> fake Storage
        +--> Boot-Control contract --> fake Boot Control
        +--> explicit monotonic now_ms
        +--> emitted RESET action after TX idle
~~~

| Component | Owns | Must not own |
| --- | --- | --- |
| Protocol | frame bytes, endian, lengths, CRC, parser | session state, Storage, Boot Control, concrete Transport |
| Manager | commands, state, sequence, duplicate, timeout, logical image accounting | physical addresses, ISR lifecycle, platform reset call |
| Storage | logical candidate bytes and physical alignment/bounds | protocol state |
| Boot Control | MCUboot pending/confirmation state | image receive/write |
| Later glue | backend lifecycle, byte feed/drain, TX idle, reset execution | parser or direct Storage/Boot-Control calls |

No Phase 3 portable file may include MCUboot, HC32, CherryUSB, USB, UART or CAN
headers. Test fakes remain in Tests; no production fake framework is added.

## Approved public API semantics

Wire constants and the initial codec identifiers exercised by the 3A RED test
are frozen in 3A. Parser and Manager identifiers are frozen in their 3B/3C
review units. The approved contract is:

- initialize one caller-allocated Manager from static configuration, Storage,
  Boot Control and numeric device information;
- feed borrowed RX bytes and return consumed count; retain no caller pointer;
- expose Manager-owned TX bytes until the caller reports consumed bytes;
- accept partial TX consumption without re-encoding or losing bytes;
- notify disconnect, aborting a volatile session without marking pending;
- notify TX idle separately from byte consumption;
- poll with explicit monotonic now_ms; portable code owns no clock callback;
- expose/take a bounded lifecycle action such as RESET;
- query state and last stable error for diagnostics/tests.

While one response is pending, no second command executes. Coalesced bytes after
the completed request remain unconsumed until the caller drains the response.

Static-memory proposal:

- RX frame buffer: 532 bytes;
- TX response/cache buffer: 532 bytes;
- staging/readback work buffer reused by state: 512 bytes;
- complete Manager object: at most 2048 bytes;
- no malloc/free and no function-local frame-sized arrays.

## Protocol and Manager behavior

The normative draft is docs/protocol/PROTOCOL_V1.md.

~~~text
IDLE
  -- HELLO(seq=0) --> NEGOTIATING
  -- BEGIN --------> PREPARING --> RECEIVING
  -- END ----------> VERIFYING --> READY_TO_COMMIT
  -- COMMIT -------> COMMITTING --> COMPLETED -- TX idle --> RESET action

Any active state -- ABORT/disconnect/timeout --> ABORTED --> IDLE
Any unrecoverable Storage/Boot-Control/internal failure --> ERROR
ERROR -- ABORT --> IDLE
~~~

Here, active means a state before successful Boot-Control COMMIT. COMPLETED has
already changed MCUboot trailer state: ABORT is rejected, and disconnect or
timeout does not claim to undo pending state or emit RESET before TX idle.

PREPARING, VERIFYING, COMMITTING and ABORTED are short synchronous states but
remain explicit for diagnostic and transition assertions.

Sequence and duplicate rules:

- HELLO uses sequence zero; next expected sequence is one.
- One accepted request produces one cached response.
- An identical duplicate replays that response without repeating side effects.
- Conflicting reuse, future sequence or stale sequence is rejected.
- CRC/sequence/frame errors do not reach command handlers.
- Three consecutive recoverable frame/CRC/sequence errors abort the session;
  a valid expected request resets the count.

Storage/order invariants:

- BEGIN validates metadata before erase_all and is the only erase command.
- DATA offset equals received logical bytes.
- DATA sizes are independent of Storage alignment; the Manager writes aligned
  prefixes from its staging buffer and rejects an alignment larger than that
  buffer.
- Invalid CRC/state/sequence/offset/size causes no Storage write.
- END reads exactly image_size, validates CRC and never calls Boot Control.
- COMMIT before READY_TO_COMMIT never calls Boot Control.
- Successful COMMIT calls test-pending once; duplicates only replay response.
- ABORT, timeout and disconnect before successful COMMIT leave the image not
  pending.
- Boot-Control failure enters ERROR and emits no RESET; Phase 3 does not claim
  that a lower-level partial trailer write is reversible.

## Files and components

Current and planned production files:

~~~text
components/fw_update/
├── include/fw_update/
│   ├── protocol.h        # implemented in 3B
│   └── manager.h         # implemented through lifecycle actions in 3D
└── src/
    ├── protocol.c        # implemented in 3B, including CRC
    └── manager.c         # implemented through reliability/commit in 3D
~~~

CRC implementation remains in `protocol.c`; Manager uses its public incremental
API, but the small protocol-defined algorithm does not justify another target
or source-file split.

Planned tests/documents:

~~~text
docs/protocol/PROTOCOL_V1.md
docs/adr/ADR-003-protocol-v1-boundary.md
Tests/Protocol/golden_vectors.csv
Tests/Protocol/verify_golden_vectors.py
Tests/fw_protocol_tests.c
Tests/fw_update_manager_tests.c
~~~

The deterministic 10,000-case corpus is intentionally part of
`fw_protocol_tests.c`: the C parser is exercised directly under sanitizers, so
no second Python protocol implementation or stored random corpus is required.

Test fakes stay inside the two test translation units unless reuse measurably
reduces code.

Expected modifications:

- components/fw_update/CMakeLists.txt
- Tests/CMakeLists.txt
- components/fw_update/include/fw_update/error.h
- Tests/Architecture/check_portable_dependencies.py
- roadmap/status documents and tracked G3 evidence

No App, Boot, Platform, MCUboot upstream or Flash-layout file changes in Phase 3.

## API impact

- Adds Protocol and Manager public contracts.
- Extends named internal result codes without changing existing Phase 2 values.
- Adds no concrete Transport backend API and no platform reset/clock vtable.
- Storage and Boot-Control contracts remain unchanged unless a real defect is
  proven; any change requires G2 regression review.

## Implementation sequence

### Phase 3A — Specification, vectors and local RED

1. Review and accept ADR-003 and Protocol V1.
2. Freeze all wire constants and payload layouts.
3. Add CRC plus complete request/response golden vectors.
4. Add HostTest targets and run a local RED against missing implementation; do
   not push a failing revision.

Review unit: documents, vectors and test skeleton only.

### Phase 3B — CRC, codec and incremental parser

1. Implement CRC reference/update/final operations.
2. Implement bounded encode/decode.
3. Implement incremental magic/header/payload/CRC parsing.
4. Pass every split point, coalesced frame, resync, maximum and malformed-header
   test.

Review unit: pure Protocol code with no Manager/Storage include.

### Phase 3C — Manager receive and verification lifecycle

1. Add caller-allocated Manager configuration/state.
2. Implement HELLO, DEVICE_INFO, BEGIN, DATA, END and ABORT.
3. Add strict offsets, alignment staging and final aligned-write padding.
4. Add bounded full-image readback CRC.
5. Inject Storage failures and corruption at deterministic call indices.

Review unit: update lifecycle without COMMIT/reset action.

### Phase 3D — Reliability and commit lifecycle

1. Add sequence/error budget and duplicate replay.
2. Add monotonic timeout and disconnect behavior.
3. Add COMMIT, single Boot-Control effect, partial TX consumption, TX-idle
   ordering and RESET action.
4. Add deterministic malformed corpus and dependency enforcement.

Review unit: reliability and lifecycle side effects.

### Phase 3E — Gate evidence

1. Run strict HostTests, malformed corpus, dependency checks and Debug/Release/
   signing CI.
2. Record Manager size and corpus/test counts.
3. Preserve logs, vectors/spec hash and results under
   evidence/host/<date>-<revision>-phase3 with SHA256SUMS.
4. Commit/push evidence, pass remote CI, then mark G3 PASSED separately.

## Automated tests

Protocol codec/parser:

- CRC empty input and ASCII 123456789.
- Exact golden bytes for every request and representative responses.
- Round trip at payload lengths 0, 1, 511 and 512.
- Every split position of minimum, DATA and maximum frames.
- Two/three coalesced frames with response backpressure.
- Garbage, overlapping magic, truncation and resynchronization.
- Payload lengths 513 and 65535 rejected before copy.
- Unknown command/flags, nonzero reserved fields and incompatible versions.
- CRC bit flips across header, payload and trailer classes.

Manager/session:

- Complete HELLO to COMMIT lifecycle.
- Every command in every invalid state.
- Zero size, aligned-up oversize and UINT32_MAX overflow.
- DATA gaps, overlap, out-of-order and past-end ranges.
- DATA lengths independent of Storage alignment, including multiple frames
  required to fill one aligned write.
- Storage alignments 1, 4, 8, 16, 24, 256 and 512; final tail remainders around
  each alignment; alignment above the 512-byte work buffer rejected at BEGIN.
- Readback corruption and Nth erase/write/read failure.
- Duplicate BEGIN/DATA/END/COMMIT with byte-identical response and unchanged
  fake call counts.
- Conflicting duplicate and future/stale sequence.
- Timeout at timeout-1, timeout and monotonic tick wrap.
- Disconnect in NEGOTIATING, RECEIVING and READY_TO_COMMIT.
- Disconnect after successful COMMIT preserves pending state and emits no RESET
  before the response-drain/TX-idle condition.
- Partial TX consumption; RESET withheld until TX idle.
- Boot-Control failure emits no RESET and enters ERROR without claiming trailer
  rollback.
- ABORT/timeout/disconnect before successful COMMIT never mark pending or add
  an erase.

Deterministic malformed corpus:

- At least 10,000 cases per gate run.
- Fixed seed 0x5EED3001 recorded in evidence.
- Input lengths 0..1024 with structured mutations and random garbage.
- Deterministic oracle: no frame, one error or one decoded frame.
- The `fw_protocol` CTest invokes the C parser directly; no second Python
  protocol implementation or stored random corpus is maintained.

## HIL tests

None. G3 claims no real-bus, ISR, Flash timing or reset behavior. Existing G0/G2
evidence remains mandatory and CI-verified.

## Fault injection

- Parser/header/payload/CRC corruption.
- Split/coalesce, short TX consumption, busy and disconnect.
- Nth Storage erase/write/read failure and readback corruption.
- Boot-Control failure and timeout in every persistent state.
- Duplicate command delivery before and after TX drain.

No nondeterministic failure may be retried into a pass. The fixed seed, exact
command and sanitizer/assert output are retained so a failure is reproducible.

## Acceptance criteria

G3 passes only when all are true:

- Protocol V1 and ADR-003 are Accepted with exact golden vectors.
- Strict HostTests pass under Werror, ASan and UBSan.
- The 10,000-case fixed-seed corpus passes.
- Every representative frame passes at every byte split; coalesced behavior is
  identical.
- Maximum payload/frame are 512/532; oversize is rejected before copy.
- sizeof(struct fw_update_manager) is at most 2048 bytes and no dynamic
  allocation is used.
- Arbitrary DATA chunk sizes are staged into aligned Storage writes; final
  unaligned tails are padded only physically and logical size/CRC stay exact.
- Invalid/malformed/duplicate commands never repeat erase, write, verify,
  pending or reset side effects.
- COMMIT calls Boot Control once and RESET appears only after response drain and
  TX idle.
- Portable Protocol/Manager contains no HC32, MCUboot Flash, CherryUSB, USB,
  UART or CAN dependency.
- Debug/Release/signing CI and immutable host-evidence verification pass.

## Required evidence

- accepted protocol/ADR revision and exact golden vectors;
- sanitizer CTest output;
- corpus seed/count/command/result;
- dependency checker output;
- Manager size and configured limits;
- Debug/Release/signing CI run;
- exact source revision, clean state and evidence SHA256SUMS.

## Exit gate

G3 is a hard gate. Phase 4 cannot be marked IN_PROGRESS until the evidence
commit passes remote CI and STATUS.md records G3 PASSED.

## Rollback point

9d29ee9843c711d7f853d7c24a7df58be8e8e1e9, the G2-passed revision. No hardware
recovery is required because Phase 3 does not change target state.

## Risks and mitigations

| Risk | Priority | Mitigation |
| --- | --- | --- |
| Parser overflow/desync | P0 | fixed maxima, pre-copy length checks, split tests, sanitizer corpus |
| Duplicate side effects | P0 | cached response replay and fake call-count assertions |
| Unaligned final image | P0 | aligned-up BEGIN check, erased tail pad, 0/1/2/3 tests |
| Layer responsibilities blur | P1 | separate source/headers and dependency checks |
| Premature Transport API does not fit USB | P1 | byte boundary only until first real backend |
| Host metadata is mistaken for trust | P1 | mark advisory; signed compatibility decision before G5 |
| Stop-and-wait throughput is low | P2 | measure at G5; add windows only with evidence |
| Corpus is nondeterministic | P2 | fixed seed/count and retained raw test output |

## Plan review checklist

- [x] ADR-003 boundary accepted or revised.
- [x] Header, endian, CRC, sizes and IDs accepted.
- [x] Common status response instead of separate ACK/NACK accepted.
- [x] Strict DATA and no V1 protocol fragmentation accepted.
- [x] Final-write padding accepted.
- [x] Byte ingress/egress accepted instead of a fake-only Transport vtable.
- [x] Explicit now_ms, TX-idle and emitted RESET accepted.
- [x] Host hardware/board metadata classified as advisory.
- [x] 2048-byte Manager and 10,000-case corpus limits accepted.
- [x] G3 remains host-only with no USB/UART/CAN/App integration.

## Recommended immediate next action

Execute only Phase 3E immutable G3 host evidence and remote CI. Do not start
Phase 4 or mark G3 PASSED before that evidence revision passes remotely.

## Phase 3A execution result

- ADR-003 and Protocol V1: accepted.
- Golden vectors: 19/19 verified by the stdlib-only verifier.
- Existing HostTests before RED reconfiguration: 7/7 passed.
- HostTests configuration with the new target: passed.
- Expected RED: `fw_protocol_tests.c` fails to compile because
  `fw_update/protocol.h` does not exist until Phase 3B.
- Failure classification: intentional test-first `build.compile.error`; product
  regression: no.
- Production source changed: no; Phase 3B started: no; HIL required: no.

## Phase 3B execution result

- Added the portable Protocol V1 public constants, CRC-32/ISO-HDLC, exact-frame
  encode/decode and caller-allocated incremental byte-stream parser.
- No allocation, packed-struct overlay, transport, Storage, Boot-Control, MCUboot
  or HC32 dependency was introduced.
- All 19 Golden Vectors decode and re-encode byte-identically.
- Payload lengths 0/1/511/512, every split of minimum/DATA/maximum frames,
  byte-at-a-time input, coalesced frames and overlapping-magic resync pass.
- Oversized lengths 513/65535, invalid flags/reserved, truncated/extra frames
  and Header/Payload/CRC corruption are rejected deterministically.
- Strict Werror + ASan/UBSan HostTests: 8/8 passed.
- Portable dependency check, Debug/Release ARM builds and image verification:
  passed; existing Boot/App sizes were unchanged because Protocol is not linked
  into the runtime Application.
- C/C++ review: no blocking correctness, memory-safety, portability or API
  finding. HIL required: no; Phase 3C started: no; commit/push: no.

## Phase 3C execution result

- Added a caller-allocated Manager with HELLO, DEVICE_INFO, BEGIN, DATA, END and
  ABORT; COMMIT remains intentionally unsupported.
- Host offsets are used only for strict logical-offset comparison. Storage
  writes use Manager-owned offsets and the bounded Secondary Storage contract.
- Arbitrary DATA chunks are coalesced into aligned writes for alignments
  1/4/8/16/24/256/512; exact and boundary-tail cases pass with erased-value
  physical padding excluded from logical size/CRC.
- END reads only logical image bytes in bounded 512-byte blocks and verifies
  incremental CRC before entering READY_TO_COMMIT.
- Deterministic erase, second-write, second-read and read-corruption injection
  produce STORAGE_ERROR/VERIFY_ERROR and no Boot-Control call.
- Phase 3C advertises only READBACK_CRC and STRICT_DATA. TEST_UPGRADE remains
  clear until Phase 3D implements COMMIT.
- Manager size on the 64-bit Host ABI: 1720 bytes; compile-time limit: 2048.
- Strict Werror + ASan/UBSan HostTests: 9/9 passed. Portable dependency check,
  Debug/Release ARM builds and signed-image verification passed.
- Review corrections: explicit unsigned capability handling, truthful Phase 3C
  capability reporting and synchronous Storage semantics in the public API.
- HIL required: no; Phase 3D started: no; G3 passed: no; commit/push: no.

## Phase 3D execution result

- Added expected sequence, exact previous-request replay and the bounded
  three-error CRC/frame/sequence budget. Accepted semantic errors advance;
  framing/version/sequence rejections do not repeat command side effects.
- Reused Parser and TX frame storage for the current request and cached request;
  only the maximum 60-byte encoded response is retained separately. No second
  532-byte request buffer or dynamic allocation was added.
- Added explicit wrapping `now_ms` feed/poll semantics, timeout deferral while a
  response is pending, and disconnect handling before/after successful COMMIT.
- Added COMMIT through Boot-Control, byte-identical duplicate replay, partial TX
  consumption, independent TX-idle notification and a single emitted RESET
  action after both success-response conditions hold.
- Exact duplicate HELLO/BEGIN/DATA/END/COMMIT/ABORT tests prove erase/write/read/
  pending side effects remain single-shot. Boot-Control failure produces
  BOOT_CONTROL_ERROR, remains replayable and emits no RESET.
- Fixed-seed parser corpus: 10,000 cases at `0x5EED3001`; 3,920 decoded frames,
  1,960 format errors and 1,960 CRC errors under ASan/UBSan.
- Manager Host ABI size: 1,824 bytes; compile-time maximum: 2,048 bytes.
- Strict HostTests 9/9, Golden Vectors 19/19, portable dependency check, scoped
  cppcheck, Debug/Release ARM builds and signed-image verification passed.
- HIL required: no; Phase 3E started: no; G3 passed: no; commit/push: no.
