# Phase 5 portable host delivery checks

Linux remains a direct-run development delivery: the locked Release CLI and GUI
built successfully, and the CLI help path executed without installation.

The Windows portable archive contains exactly `README.txt`, `SHA256SUMS`,
`hc32-updater.exe`, and `hc32-updater-gui.exe`. Both binaries are PE32+ x86-64,
the ZIP integrity check passed, and the hashes of the archived executables match
the input executables. The unsigned ZIP SHA256 is
`4a501fa32a3b229ccd7373f86cfd9ee704038865f4eb81f3fd2712469552f01f`.
The generated ZIP is not retained in the repository; its construction, contents,
integrity and hashes are retained in the logs.

This is only a mechanical packaging result. Production Authenticode inputs were
not supplied, the EXEs are not release-signed, and no clean-Windows run was
performed. A fresh post-review cross-build attempt was also retained as an
infrastructure failure because the current Linux host lacks the MinGW CRT/import
libraries; LLVM supplied `dlltool` compatibility but cannot replace that runtime.
