# App I2C2 and BQ40Z50 bring-up

This bundle preserves the 2026-09-03 pre-refactor HIL that validated the HC32
I2C2 transaction path, found the configured BQ40Z50 at `0x0C`, corrected the
ManufacturerAccess byte order and read the gauge identity.

The run did not validate HUSB238, MP2762A, the second BQ40Z50 at `0x0B`, or the
later `App/Services` and `App/Diagnostics` refactor. Its result is therefore
partial engineering evidence, not release qualification.
