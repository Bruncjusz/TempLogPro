# TempLogPro rearchitecture plan

Date: 2026-08-05

## Snapshot

Current source snapshot was saved before edits:

```text
backup\rearchitecture_20260805_020959
```

The project is not a git repository, so no git tag could be created.

## Verified hardware facts

| Area | Verified fact |
|---|---|
| MCU | STM32L152RCT6 |
| Winbond | W25X40CLNIG responds by JEDEC ID on SPI1 |
| SPI1 pins | CS PA4, SCK PA5, MISO PA6, MOSI PA7 |
| Buttons | START PA0, STOP PA1, active low with pull-up |
| LEDs | Green PC0, red PB8 |
| USB | PA11 USB_DM, PA12 USB_DP |
| LCD | STM32 LCD peripheral, segment/icon map in `Src/main.c` |
| RTC/LCD clock | Existing RTC/LCD fallback avoids dead LCD after power loss |
| Temperature sensor | Existing code uses LM75-like read path through hardware bridge |

Unknowns are still documented separately and must not be guessed.

## Preserved working code

| Module/file | Preserved responsibility |
|---|---|
| `Src/main.c` | low-level register access, LCD segment map, SPI1/Winbond bridge, ADC/VDD, USB bring-up glue |
| `Src/templogpro_rtc.c` | RTC init and timestamp handling |
| `Src/templogpro_logger.c` | append-only records, metadata copies, CRC, sector-boundary fix |
| `Src/templogpro_config.c` | current config parser/storage |
| `Src/st_usb/*` | working ST USB MSC base used by current firmware |

## New module boundaries

Target architecture:

- hardware bridge: pinout, clocks, LCD RAM, LEDs, ADC, SPI1, sensor reads
- button FSM: only stable physical-to-logical event generation
- app state machine: READY/RUN/PAUSE/STARTING/STOPPING/CLEARING
- menu model: visible card order and card semantics
- LCD renderer: maps semantic card state to text and icon mask
- logger storage: events, measurements, metadata, active-time accounting
- alarms: global alarm lifecycle and report entries
- USB MSC: visible files `TEMPLOGPRO.HTML`, `DATA.CSV`, `README.TXT`
- USB CDC: framed protocol for config and RTC sync
- report generators: HTML, CSV, README/report data; PDF export is done on the PC from HTML
- diagnostics: boot stage, last error, optional logs

## Implemented in this stage

- Button FSM rewritten in `Src/templogpro_buttons.c`.
- Public button events renamed to START/STOP semantics.
- Debounce set to `50 ms` for press and release.
- Long action emits once after `3000 ms` and locks out until release.
- Short action emits only after stable release.
- App no longer wakes LCD immediately on button press.
- Short press on dark LCD wakes only and is consumed.
- Long START/STOP on dark LCD still works.
- START/STOP/CLR use one nonblocking action animation after 3 s confirmation.
- Default menu sequence changed to:
  `TEMP`, `STATE`, `MIN`, `MAX`, `AVG`, `LOW`, `HIGH`, `TIME`, `REC`, `INT`, `BAT`, `CLR`.
- Logger gained `RESUME` event so START from PAUSE can continue the same report.
- Alarm card and automatic `noA` display were removed from default menu.
- Logger metadata version 2 stores active runtime and current RUN segment start.
- Runtime LCD card now uses active time and excludes PAUSE intervals.
- Legacy logger metadata version 1 remains readable.
- USB MSC exposes:
  `TEMPLOGPRO.HTML`, `DATA.CSV`, `README.TXT`.
- `TEMPLOGPRO.HTML` is generated dynamically from the current CSV snapshot and
  contains an offline tabbed report, vector SVG charts, a dark/light theme and
  browser-side print-to-PDF views.
- `DATA.CSV` remains available as raw data for Excel, Python, LibreOffice or
  custom plotting.
- `README.TXT` documents visible files, alarm/event codes and basic button use.
- `TEMPLOGPRO.HTML` now uses local tabs for report, detailed chart, data table,
  device/session info and USB configuration.
- Report printing hides the data table and USB/config sections.
- Data printing hides the report sections and expands the table for PC-side PDF
  generation.
- Firmware-side PDF generation was removed from the MSC image to reduce MCU code
  and RAM pressure.
- HTML report version `0.8.5-apple` opens in dark mode by default, with an
  Apple-like visual style and a top theme toggle.
- Browser/PDF output is forced to a white report theme before printing, then the
  previous screen theme is restored.
- `Zapisz dane PDF` now prints the report as page 1 and the data table after it.
- The detailed chart tab now has from-to date/time controls that filter both SVG
  charts locally from the embedded CSV.
- The report creation date is generated in the browser and shown in the header
  plus device/run information.

## Deliberate limitations after this stage

- Alarm lifecycle still uses simple per-measurement flags.
- CDC/Web Serial config transport is implemented for config and RTC sync.
- CDC logger control commands are still not implemented.
- PDF export depends on the PC browser print engine. The STM32 exposes the data
  and HTML UI, but it no longer exposes generated PDF files.
- Config format still needs version bump for eco mode, heartbeat options,
  visible-card mask, alarm hysteresis and delay.
- Battery operation still needs another on-device power-loss/startup test.

## Verification

```text
python tests\button_fsm_vectors.py
button_fsm_vectors: OK
```

```text
make -j24 all
text 77276
data   308
bss  12208
```

After flashing through ST-Link:

```text
boot_stage = 10 (MAIN_LOOP)
g_app_state = 1 (RUN)
g_lcd_view = 0 (TEMP)
g_last_flags = 1 (VALID)
g_have_last_measurement = 1
```

Runtime-stage flash verification:

```text
boot_stage = 10 (MAIN_LOOP)
g_app_state = 1 (RUN)
g_lcd_view = 0 (TEMP)
g_action_active = 0
logger metadata magic = 0x544C4F47
logger metadata version = 2
logger state = LOGGING
```

HTML/MSC-stage flash verification:

```text
make -j24 all
text 61404
data   308
bss   6592

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)
g_app_state = 1 (RUN)
g_lcd_view = 0 (TEMP)
g_action_active = 0

USB volume label = TEMPLOGPRO
USB files:
TEMPLOGPRO.HTML  7224 bytes
DATA.CSV       2704 bytes
README.TXT     1536 bytes
```

PDF-stage flash verification:

```text
make -j24 all
text 69120
data   308
bss  10704

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)

USB volume label = TEMPLOGPRO
USB files:
TEMPLOGPRO.HTML  10339 bytes
DATA.CSV        5820 bytes
REPORT.PDF      4096 bytes
README.TXT      1536 bytes
```

`REPORT.PDF` was copied from the device and checked with Poppler:

```text
PDF version = 1.4
Pages = 1
Page size = A4
Encrypted = no
Render to PNG = OK
```

Tabbed HTML/DATA.PDF-stage flash verification:

```text
make -j24 all
text 75196
data   308
bss  10720

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)

USB files:
TEMPLOGPRO.HTML  19468 bytes
DATA.CSV       10781 bytes
REPORT.PDF      4096 bytes
DATA.PDF        4096 bytes
README.TXT      1536 bytes

HTML script syntax = OK
REPORT.PDF pdfinfo/render = OK
DATA.PDF pdfinfo/render = OK
```

PC-side PDF/theme-stage flash verification:

```text
make -j24 all
text 68340
data   308
bss   6592

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)

USB files:
TEMPLOGPRO.HTML  29181 bytes
DATA.CSV       17915 bytes
README.TXT      1536 bytes

HTML script syntax = OK
HasSvg = True
HasCanvas = False
HasDark = True
HasDenseGrid = True
Has firmware-side PDF files = False
```

This supersedes the earlier firmware-generated PDF stage. The device now exposes
HTML/CSV/README only; report and data PDFs are created by the PC browser from
`TEMPLOGPRO.HTML`.

Apple-style theme/chart-range flash verification:

```text
make -j24 all
text 74668
data   308
bss   6592

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)

USB files:
TEMPLOGPRO.HTML  46976 bytes
DATA.CSV       29641 bytes
README.TXT      1536 bytes

LightGrayBg = True
DarkGrayBg = True
NoOldMaxWidth = True
ChartFullWidth = True
DynamicViewBox = True
BeforePrintRedraw = True
DataPdfHasReport = True
DataPrintHidesRunInfo = True
HTML script syntax = OK
```

Chrome/Poppler data-PDF verification from the mounted `TEMPLOGPRO.HTML`:

```text
Page size = A4
Page 1 text = TempLogPro report, summary cards and chart labels
Page 2 text = Dane table rows start immediately
Rendered pages 1-2 to PNG = OK
```

PL/EN and chart-axis flash verification:

```text
make -j24 all
text 80720
data   308
bss   6592

python tests\button_fsm_vectors.py
button_fsm_vectors: OK

python tests\logger_runtime_vectors.py
logger_runtime_vectors: OK
```

After flashing through ST-Link and enumerating in Windows:

```text
boot_stage = 10 (MAIN_LOOP)

USB files:
TEMPLOGPRO.HTML  66795 bytes
DATA.CSV       43581 bytes
README.TXT      1536 bytes

HTML version = 0.8.7-config
PL/EN toggle = present with CSS-rendered flag icon
Time labels with full date below = present
High-density sample dots = present
Events table = present
USB settings tab = full CONFIG.TXT-compatible form and Web Serial payload
Start/stop chart markers = present
Out-of-limit chart segments = red
Config limit lines = present when limits are visible in chart range
Run block page-break guard = present
HTML script syntax = OK
```

Chrome/Poppler PDF verification:

```text
Report PDF pages = 1
Data PDF pages = 18
Report page 1 = summary and chart with time/date axis labels
Data PDF page 2 = events table plus data table
Data PDF page 2 = data table starts immediately
Rendered report pages 1-2 = OK
```

CDC/Web Serial stage was started in `docs/usb_cdc_config_protocol.md`.

Composite MSC+CDC verification:

```text
make -j24 all
text 103748
data    520
bss    9680
```

After flashing through ST-Link and enumerating in Windows:

```text
USB volume label = TEMPLOGPRO
TEMPLOGPRO.HTML = 110977 bytes
DATA.CSV = 73511 bytes
README.TXT = 2048 bytes
CDC port = USB Serial Device (COM3)
PING = OK PING
GET STATUS = STATUS usb=cdc msc=1
CONFIG write = OK CONFIG
HTML script count = 3
HTML script syntax = OK
```

No logger-data erase was performed by these edits. During CDC verification the
config sector was rewritten once with the same visible values and returned
`OK CONFIG`.
