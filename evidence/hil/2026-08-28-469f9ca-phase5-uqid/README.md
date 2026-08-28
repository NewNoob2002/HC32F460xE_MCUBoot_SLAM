# Phase 5 per-chip USB serial HIL

This bundle verifies that Application and Boot recovery expose the same stable
HC32F460 UQID-derived USB serial on the current revision-2 board using J-Link
20781318. The run also retained the three raw UQID words, USB descriptor reads,
CLI info output for both modes, and byte-exact pre/post Flash restoration.

The directory originally also contained an experimental Debian application
package check. That delivery path was cancelled in favor of Linux direct-run;
its package-only logs and scripts were removed before this node was finalized.

Result: Application and Boot recovery both reported
`HC32F460-55463233FF035043FFFF7309`. The 483,328-byte post-restore image is
byte-identical to the pre-HIL backup with SHA256
`68749a28839ef5b1e98473e014b611261add7434adb3dbc4419793c3fabc9b05`.
