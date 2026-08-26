# Phase 3 Protocol Core Host Evidence

This immutable bundle records the local G3 verification for source revision
`e7899bd8c2f96b138f6daab7d491268286e40a14`. The source worktree was clean
before the verification matrix started.

## Result

- Strict Werror + ASan + UBSan HostTests: 9/9 PASS.
- Fixed-seed malformed corpus: 10,000 cases at `0x5EED3001`; 3,920 decoded
  frames, 1,960 format errors and 1,960 CRC errors.
- Protocol V1 Golden Vectors: 19/19 PASS.
- Portable dependency, clang-format and scoped cppcheck checks: PASS.
- Manager Host ABI size: 1,824 bytes; hard limit: 2,048 bytes.
- Debug firmware/signing: PASS; Boot 26,804 B, App 9,796 B.
- Release firmware/signing: PASS; Boot 24,112 B, App 9,224 B.
- Release signing policy: missing key and ECDSA-P384 key were rejected.
- HIL: not required by G3 and not performed.

The local Release build used the ignored Debug development ECDSA-P256 key only
to exercise signing and verification. No private key is retained in this bundle.
Remote CI generates an independent temporary ECDSA-P256 key.

Verify this bundle and all retained evidence with:

```sh
python3 Tests/HIL/verify_evidence.py
```
