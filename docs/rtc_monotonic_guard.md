# RTC monotonic guard

Firmware version: 0.8.11-rtc

## Verified behavior

- The 12-byte temperature record format is unchanged.
- Logger metadata uses the previously reserved 32-bit field at byte offset 52
  for the highest timestamp already written to the W25X40.
- On startup, the RTC is compared with that high-watermark. If it is behind,
  a RAM offset prevents subsequent records from moving backwards in time.
- A runtime RTC rollback is compensated without accelerating repeated reads in
  the same second.
- A valid RTC calendar is copied across an RTC clock-source reconfiguration
  instead of being replaced immediately with the firmware build timestamp.
- An explicit time update older than stored logger history is rejected.
- `R004` records an accepted USB clock synchronization.
- `R005` records activation of the rollback guard.
- The logger metadata remains wear-levelled in the existing two sectors. No new
  data-sector erase path was added.

## Assumptions and limits

- The highest timestamp in existing records is assumed to be trustworthy. If a
  previously stored timestamp is accidentally far in the future, setting an
  earlier clock is intentionally blocked until the logger data is cleared.
- The guard preserves ordering, but it cannot reconstruct real elapsed time
  while every power source and the RTC backup domain were absent.
- The CR3450 voltage drop observed while USB is connected is a separate power
  path issue. Firmware timing changes cannot safely correct reverse current or
  missing source isolation on the PCB.

## Diagnostic fields

`DEVICE.TXT` reports:

- `RTC rollback guard`
- `RTC monotonic floor`
- `Log time high-watermark`

`TIME.TXT` also reports the guard state and floor.

## Device verification (2026-08-20)

- Debug and Release configurations rebuilt without warnings.
- Release ELF SHA-256:
  `3A50D0625EB0D86933E7B3E66E347D434BA8E1DECD4EC220D9ED406B7B480666`.
- Release firmware programmed and verified over SWD without erasing the
  external W25X40 logger storage.
- Existing logger history survived the update: 8023 temperature samples were
  still available after programming and a hardware reset.
- `SET TIME 2026-01-01 00:00:00` was rejected with `ERR E005` because it was
  older than the stored logger timeline.
- A valid forward time update was accepted and produced record 8028 with code
  `R004`; the temperature sample count remained 8023.
- After a hardware reset, USB MSC and CDC returned, `PING` returned `OK PING`,
  and the logger state remained STOPPED.
- The generated `TEMPLOGPRO.HTML` passed a JavaScript syntax check and contains
  the 0.8.11-rtc version plus the R004/R005 descriptions.

Host regression tests passed:

- `tests/rtc_monotonic_vectors.py`
- `tests/logger_runtime_vectors.py`
- `tests/button_fsm_vectors.py`
