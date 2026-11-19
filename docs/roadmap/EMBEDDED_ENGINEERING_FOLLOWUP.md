# Embedded engineering follow-up acceptance (2026-11-06)

Reviewed HEAD: `78b9dae79bf38903cb1ae5cd8e1e7829b2ddcba4` plus the current uncommitted implementation.

Scope: `mcu-baremetal`, resource tier T1. The reviewer did not author production changes, edit source, commit, flash or control hardware. Production files were hashed before the run and remained unchanged throughout. The final Python gate and its regression tests were re-read and re-run after the author's corrections. `source-manifest.json` and `final-source-manifest.json` identify the inspected content.

Status: **software repair items 1–4 completed and independently verified**.

The user authorized this follow-up against `78b9dae` after the architecture/code
review. Independent QA recommends PASS for this software scope. Formal overall
W10 acceptance is **not declared**: the separate [167-item BARR-C review](BARR_C_2018_REVIEW.md)
completed with 57 FAIL and 11 BLOCKED entries (80 PASS, 9 explicit project
rules, 10 N/A). These existing deviations have not been approved by the agent.
Final human sign-off and the historical cumulative resource exception remain open.

No firmware security boundary, Flash layout, signing key, updater wire format,
or hardware state was changed. Source current capability is now decoded below
the business boundary. Diagnostic TLVs retain their tags, order and raw bytes.
Board simulation is `BLOCKED_BY_MODEL`, as explicitly selected by the user.

Implementation assistance: OpenAI Codex, GPT-6 family (exact runtime build not
exposed). Independent QA used a separate agent and did not author production
changes. Human code ownership/sign-off remains pending; no DCO sign-off is added.

Independent logs and source fingerprints are retained locally under
`build/Acceptance/independent/`; the command matrix below records their names.
The main manifest covers production sources, while the final manifest includes
the updated Python gate and regression tests.

## Requirement traceability

| Item | Independent observations | Result |
| --- | --- | --- |
| 1. Semantic power contract | HUSB238 code tables moved below `PowerDevices`; DataProc budgets `current_limit_ma` only. Raw source bytes are explicitly diagnostic and the Debug Link encoder preserves their existing TLV tag and ordering. Failed source reads preserve outputs, while DataProc falls back to 500 mA rather than consuming a stale high capability. | PASS |
| 2. Architecture enforcement | Concrete device headers are driver layer; HC32 implementation declarations no longer reside in service contract headers. The checked-in gate enforces include direction, forbidden includes/code and configured module cycles. Positive/negative cases cover contract use, direct/relative device includes, HC32 adapter include, HAL call, register access, comment/string non-matches, interface-to-driver violation, cyclic and acyclic modules. | PASS |
| 3. Static quality gate | Full production C and C++ clang-tidy runs complete under the checked-in policy. Cross-GCC include discovery carries the target machine options. The previous long functions were separated by responsibility; the GNU variadic wrapper extension was removed. CI invokes the same production formatting/static entrypoint. | PASS locally; hosted CI itself NOT_RUN |
| 4. Boundary/LED regression tests | HAL adapter tests execute production mapping through all four transfer/init entrypoints with every known BSP result and unknown negative/positive results. LED tests cover 6 battery states × 7 override inputs × 3 activity states. Production App actually consumes the tested decision. Existing Debug Link regression tests retain raw-byte compatibility checks. | PASS |

## Independently executed checks

| Command | Result | Log |
| --- | --- | --- |
| `cmake --preset HostTests --fresh` | PASS | `host-configure.log` |
| `cmake --build build/HostTests --clean-first --parallel 4` | PASS | `host-build.log` |
| `ctest --test-dir build/HostTests --output-on-failure` | PASS, 33/33; rerun after Python gate changes | `host-tests-final.log` |
| `cmake --build build/Debug --clean-first --parallel 4` | PASS | `debug-build.log` |
| `cmake --build build/Debug --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls` | PASS | `debug-verify.log` |
| `cmake --build build/Release --clean-first --parallel 4` | PASS; existing cache/external signing key | `release-build.log` |
| `cmake --build build/Release --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls` | PASS | `release-verify.log` |
| `python3 Tools/check_firmware_quality.py --build-dir build/Debug` | PASS; format 134 files, clang-tidy 40 C + 19 C++ sources | `quality.log` |
| `python3 /home/gtc/.agents/skills/embedded-engineering/scripts/arch_lint.py . --config Tests/Architecture/arch_layers.json` | PASS; 144/152 files mapped, 0 violations | `architecture.log` |
| `python3 Tests/HIL/verify_evidence.py` | PASS; 24 retained bundles | `evidence.log` |
| `cpp_cost.py check build/Debug/App/app-1.0.0.elf --tier T1` | PASS with pre-existing newlib heap/format warnings | `debug-cost.log` |
| `cpp_cost.py check build/Release/App/app-1.0.0.elf --tier T1` | PASS; no restricted runtime symbols | `release-cost.log` |
| `git diff --check` | PASS | tool output |

Two independently constructed mutations were compiled against the new tests using temporary source copies: mapping `AddressNack` to `BusBusy`, and mapping LED update errors to UPDATE. Each executable failed its intended assertion (exit -6), confirming that the new test oracles reject those regressions. No repository source was modified. Details: `mutation-oracles.log`.

## Resource evidence

Linker-reported region consumption, including the same accounting as the original baseline:

| Image | Flash B | Main RAM B | Delta from 78b9dae Flash/RAM |
| --- | ---: | ---: | ---: |
| Debug App | 73,132 | 16,288 | +276 / 0 |
| Debug Boot | 61,364 | 10,268 | 0 / 0 |
| Release App | 53,832 | 14,736 | +108 / 0 |
| Release Boot | 44,736 | 8,700 | 0 / 0 |

The original historical App Debug baseline was 71,888 B. Cumulative growth is 1,244 B, which exceeds that original batch's 1 KiB threshold by 220 B. The current follow-up delta must not be described as being within the original cumulative threshold.

## Limits

- The independent BARR-C 167-item manual review is a separate deliverable. No complete BARR-C PASS is implied by these mechanical gates.
- Board simulation: `BLOCKED_BY_MODEL`, per the user's explicit choice; no HC32F460 QEMU board model was available.
- HIL and physical USB enumeration, swap, rollback, reset and power-loss behavior: `NOT_RUN`. No hardware was operated.
- Hosted Ubuntu CI was not run here. Local tools are recorded in `build/Acceptance/independent/versions.json`; CI's installed Clang version may differ.
- The architecture scanner is source-based, does not expand the C preprocessor and cannot prove every possible indirect dependency.
- Final W10 sign-off remains a human decision; the agent adds no DCO sign-off.

## Reproducible project quality entrypoints

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ctest --test-dir build/HostTests --output-on-failure
python3 Tools/check_firmware_quality.py --build-dir build/Debug
```

The architecture CTest reads `Tests/Architecture/arch_layers.json` and enforces
include direction, forbidden include/code patterns and configured module cycles.
Its regression test first reproduced three missed direct/relative device
includes before the fix. It now also checks contracts, HC32 adapters, forbidden
calls/register access, comments/strings, and cyclic/acyclic modules. No external
skill installation is needed in CI. The external skill lint remains a separately
recorded cross-check.

The quality entrypoint needs `clang-format`, `clang-tidy`, and the compiler named
in the selected target compilation database. It discovers C++ system headers
using that compiler's CPU/FPU/ABI settings. GitHub CI installs the two analysis
tools and invokes the same entrypoint after its Debug firmware build. Hosted CI
execution is still NOT_RUN locally.
