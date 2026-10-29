# Boot-side install, REBOOT and administrator HIL

Ran on 2026-10-29 with J-Link 63728769 on the HC32F460xE V1.1 target using Debug
Boot and Application 1.0.4/1.0.5 built from the uncommitted Boot-install/admin
change set.

- Application REBOOT(recovery) re-enumerated as `cafe:0001` with the same serial;
  REBOOT(application) returned to the Application. Boot clears the retention-SRAM
  request slot, and an external reset afterwards starts the Application.
- An Application-mode CLI install restarted into Boot, installed over USB into
  Secondary, swapped and started 1.0.5, which persisted across reset. Two more
  cycles passed.
- FACTORY_ERASE with a wrong administrator key returned `VerifyError` and changed
  nothing. With the development key the device re-enumerated in Boot with its
  factory serial, unprovisioned, and Primary/Secondary/Scratch read back erased.
  It was then re-provisioned and 1.0.5 installed in Boot recovery.

Deviation: the J-Link `erase` end address is inclusive, so the preparation erase
also cleared product_kv sector `0x00076000`. Product Config was re-provisioned
with the recorded values (SN202061028 / 1.1 / 0x0002); `app_kv` was not erased.
A host reconnect race with the udev ACL (`errno 13`) was fixed and re-verified.

Final CLI state: confirmed Application 1.0.5, SN202061028, CFSR/HFSR zero.

Operator GUI follow-up (same day; observations reported by the operator, with
screenshots not retained here): GUI factory erase left the device in Boot,
unprovisioned, with all slot ends erased (log 12). Boot 1.1.0 with the fixed
boot-info record was then programmed at `0x00000000` and verified (log 13);
Primary/Secondary/Scratch and Reserved were not touched, and the device reported
`bootloader=1.1.0 application=0.0.0` (log 14). In the GUI the operator
re-provisioned 20261029 / HW1.2 / 0x0002, installed Application 1.0.7 over USB,
and saw Boot version 1.1.0 plus the BQ40Z50 0x0B battery card (charging, 94%,
8.33 V, +409 mA) with pack 0x0C not detected. Application 1.0.8 MP2762A
telemetry, 1.0.9 battery LED patterns and the Windows build were not verified
on hardware in this run. The programmed Boot predates the later review fixes
(Secondary-first factory erase order, bounded TRNG polling and cancelling
REBOOT/FACTORY_ERASE on disconnect); those fixes are host-tested only. The pre-HIL backup is retained only under the
ignored `build/local-evidence-backup/2026-10-29-boot-install-admin/` directory.
