# TempLogPro USB HTML report

Date: 2026-08-05

## Current USB mass-storage image

The device exposes a small FAT16 volume named `TEMPLOGPRO`.

Visible files:

| File | Purpose |
|---|---|
| `TEMPLOGPRO.HTML` | Offline report UI with tabs, SVG charts, Apple-like dark/light theme and PC-side print-to-PDF export |
| `DATA.CSV` | Raw logger entries for Excel, Python, LibreOffice or custom analysis |
| `README.TXT` | Device-side user guide and alarm/event code reference |

The firmware does not generate PDF files on the STM32. PDF export is intentionally
done by the user's PC from `TEMPLOGPRO.HTML` with the browser print dialog. This
keeps the MCU code smaller and leaves the chart rendering to the browser.

## HTML layout

`TEMPLOGPRO.HTML` uses local tabs:

| Tab | Content |
|---|---|
| `Raport` | Summary, full vector chart, integrated from-to range controls, integrated event legend, compact run data and report PDF print target |
| `Dane` | Scrollable events table plus data table; PDF export includes report as page 1 and tables after it |
| `Informacje` | Device, firmware, generated-file and session information |
| `Ustawienia` | Full configuration form populated from firmware config, color-coded logger controls and prepared Web Serial/CDC payload |

The chart is generated in the browser as SVG. It is not an embedded screenshot
and it does not use a canvas bitmap, so printed output remains sharp.

The logger CSV snapshot is embedded inside `TEMPLOGPRO.HTML` on purpose. Chrome and
Edge do not reliably allow a local `file://` report to fetch another local file
from a removable drive without extra user action. Embedding the CSV keeps the
report self-contained, while `DATA.CSV` remains available as a separate raw
export for spreadsheet tools.

## Theme

The HTML page opens in dark mode by default and includes a top light/dark theme
toggle. The selected theme is stored in browser `localStorage`, so the choice is
remembered on the PC that opens the report.

PDF output is generated in a light print theme. Before opening the browser print
dialog, the page temporarily redraws the SVG charts in light colors and restores
the previous screen theme after printing.

## PDF export on PC

Use the buttons in the HTML page:

| Button | Effect |
|---|---|
| `Raport skrocony` / `Short report` | Opens the browser print dialog with the compact report only, forced light theme |
| `Raport pelny` / `Full report` | Opens the browser print dialog with the report as page 1 and the data table after it |

When printing the report, the raw data table and USB/config section are hidden.
When printing the data view, the report is kept as the first page and the table
is allowed to expand for PDF output.

## Verified on Windows

After flashing the current firmware, Windows enumerated the device as:

```text
Drive: F:
VolumeName: TEMPLOGPRO
```

Example directory readback from the current device after flashing:

```text
TEMPLOGPRO.HTML  29181 bytes
DATA.CSV       17915 bytes
README.TXT      1536 bytes
```

`TEMPLOGPRO.HTML` and `DATA.CSV` sizes grow with the current logger snapshot.

HTML feature check from the mounted device:

```text
HasSvg           = True
HasCanvas        = False
HasGetContext    = False
HasReportPdfFile = False
HasDataPdfFile   = False
HasDark          = True
HasPrint         = True
HasDenseGrid     = True
HTML script syntax check = OK
```

After the Apple-style theme and chart-range update:

```text
TEMPLOGPRO.HTML  46976 bytes
DATA.CSV       29641 bytes
README.TXT      1536 bytes

HasDefaultDark   = True
HasThemeButton   = True
HasCreatedAt     = True
HasRangeControls = True
DataPdfHasReport = True
PrintForcesWhite = True
UsesAfterPrint   = True
HasAppleVersion  = True
HTML script syntax check = OK
```

After the full-width gray-theme and data-PDF page-break correction:

```text
LightGrayBg = True
DarkGrayBg = True
NoOldMaxWidth = True
ChartFullWidth = True
DynamicViewBox = True
BeforePrintRedraw = True
DataPrintHidesRunInfo = True
HasRiseIn = True
HasLineIn = True

Generated data PDF:
Page size = A4
Page 1 = report and full-width chart
Page 2 = data table starts immediately
Visual render of pages 1-2 = OK
```

After the PL/EN, events, chart-axis, USB-config and CDC update:

```text
Firmware HTML version = 0.8.11-rtc
PL/EN language toggle = OK
CSS-rendered language flag = OK
Language switch flag = Poland / USA
Report PDF in PL = OK
Report PDF in EN = OK
Data PDF in PL = OK
Chart X axis = time with full date below each visible label
Chart legend = visible START/STOP/RESUME event types only
Chart drag selection = pointer drag across SVG updates from-to range and report PDF scope
Chart sample dots = enabled with high adaptive density
Chart start/stop/resume vertical lines = enabled without in-plot text labels
Chart pause gap gray bands = enabled
Chart pause label = drawn above the grid so it stays readable
Chart resume/start anchors = synthetic SVG-only points remove visual gaps before the first next measurement
Chart pause gaps = compressed to a fixed narrow visual band sized for the PAUSE label
Chart hover cursor = nearest real sample shows temperature, date and time
Chart measured min/max/avg horizontal lines = enabled with collision-avoiding right-side axis labels
Chart red out-of-limit line segments = clipped exactly at configured limit crossing
Chart high/low alarm colors = separate high-limit and low-limit colors
Events table alarm colors = separate T001/T002/S001/E001 colors
Chart config limit lines = enabled as Range min/Range max, clipped to chart edge when outside visible range
Chart right-side labels = tighter right margin and no outside-range `<`/`>` suffixes
Range controls and legend are visually merged into the chart panel
Short report PDF = chart and run data are compressed to stay together on the first page
Report buttons = short report / full report naming
Summary labels = Temp samples vs events
Settings tab = full CONFIG.TXT-compatible form plus Web Serial read/send buttons
CDC port = USB Serial Device (COM3) on Windows
CDC PING = OK PING
CDC GET STATUS = STATUS usb=cdc msc=1
CDC LOGGER STATUS = LOGGER STATUS app=... logger=... records=...
Settings tab can now read logger status and send guarded logger start/pause/clear
commands through Web Serial. Clearing requires browser confirmation and the
firmware-side `LOGGER CLEAR CONFIRM` command.
CDC CONFIG write = OK CONFIG
Run data block = no split between heading and table
HTML script syntax check = OK
```

Definitions used by the report:

| Label | Meaning |
|---|---|
| `Pomiary temp.` / `Temp samples` | Valid temperature measurements only |
| `Zdarzenia` / `Events` | Entries other than normal `M000` measurements: start/resume/pause, alarms and errors |

## Important implementation note

The MSC storage layer accepts host `WRITE(10)` requests and ignores them. This
matches the previous stable MSC behavior and avoids Windows retry loops while
still preventing host writes from changing Winbond logger data.

## Not implemented yet

- More advanced alarm lifecycle with hysteresis and delay.
