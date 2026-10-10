# LCD, action cancellation and USB fault follow-up

## Confirmed code changes

- Config v4 uses the previously reserved 16-bit field at offset 6 for the
  12-bit LCD menu mask. Encoded size remains 44 bytes; CRC remains at offset 42.
  Earlier versions retain all previous settings and enable all menu cards.
- HTML menu checkboxes, corrected startup labels, menu filtering in both
  directions, and remaining record capacity in percent/count.
- START/STOP/CLEAR countdowns cancel on button release/change. Failed STOP cannot
  fall through to CLEAR; failed CLEAR cannot show success. No new LCD captions.
- Temporary RAM-only segment probe is USB-only and expires. It does not write
  configuration or pause the logger. Physical MIN/MAX mapping is NOT yet verified.

## Live fault evidence before the next flash

User reported about 14 mA from battery with USB and battery voltage about 0.7 V.
Advised disconnecting the primary battery and testing on USB only. No power-path
fix is claimed; MCU VDD below is NOT a measurement at the battery terminals.

SWD on the preceding selector/probe build:

- g_diag: rtc=1, sensor=1, flash=1, usb=1, stage=10, reset_flags=0x0C000000,
  vbus=1, last_error=5 (FLASH_WRITE), logger_state=1, records=1405.
- g_app_state=6 (ERROR).
- g_hw_flash_failure: operation=1 (write), address=0x71EC,
  result=4 (verify mismatch), tick=551 ms.
- CDC: VDD=3295 mV, clock=32 MHz, logger=LOGGING but app=ERROR,
  capacity=42284, records=1405, samples=1400, interval=5 s.
- No explicit software-reset call was found in the application/button handlers.
  Reset flags alone do not establish when the user's reset happened.

Read-only sector backup:
`backup/before_lcd_selector_20260922_004956/failed_sector_7000.bin`.
The failed slot starts at offset 0x1EC in this sector. It is not erased:
`AA FD 8D 6A 00 00 01 00 7E 05 00 00`; following slots are FF.
This can prevent a subsequent NOR write at the same address. Do not erase this
sector without preserving the valid records before the failed slot. Logger
storage and the power-path issue need separate recovery/diagnosis; these UI
changes do not fix either. No format was performed during this follow-up.

The one-segment test requested from the user was 091 = RAM6 bit15, currently
named MAX. It was explicitly disabled again during the USB/Err investigation.
Need physical photo confirmation before assigning replacement segment addresses.

## Recovery and USB serialization

Full 524288-byte backup saved as `winbond_before_slot_repair.bin` in the same
backup directory. SHA256:
`b5271a5db643d65b917ae304e78701ed1ae3ff56aa17c420b3701e9fbfb97372`.
All first 1405 records pass CRC and sequence checks. The user authorized clearing
memory if necessary. No full format was needed: sector 0x7000 was erased and its
first 492 bytes (41 valid records) restored. Readback matches the backup exactly,
and the remainder is erased. Metadata and all other sectors were not rewritten
by recovery. After restart the logger advanced to 1420 records in RUN on USB.

Code inspection also found an unguarded shared-SPI path: USB MSC file reads run
inside USB_LP_IRQHandler and could interrupt main-loop Winbond operations.
Flash init/read/write/erase now save and mask IRQ20, restoring its previous enable
state afterwards. SysTick and other interrupts remain enabled for timeouts and
buttons. USB service can be delayed by a bounded flash operation. This fixes a
code-level concurrency risk; it does not prove the cause of the battery voltage
drop or distinguish that cause from power-loss damage to the failed record.

Hardware checks: CDC configuration accepted mask=5/startup=2 and read it back;
original mask=4095/startup=0 then restored. Reading the HTML/CSV and writing
configuration while logging did not produce another Err during this short test.
HTML on the real USB volume contains lcdMenu, records and capacity=42284.

Tests: 229 USB/sensor/config, 111 LCD, 218 action/menu/capacity, 36 flash IRQ guard
assertions on the Release ARM image; button FSM and four 63-check idle variants;
Playwright settings tests at 1440 and 390 px, including PL/EN and free-space limits.
Physical MIN/MAX/AVG mapping and battery-current behavior remain unverified.
