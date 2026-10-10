> **Note (2026-10-11):** card order, threshold/interval icons and LED timing changed later; current description: `DOKUMENTACJA.md` section 3.

# LCD menu and icon usage

Updated: 2026-10-09

## LCD icon map

The icon map follows `temptale-ultra_LCD.pdf` (TempTale Ultra LCD RAM Map) in
the project root. The firmware uses named icon bits through
`templogpro_hw_lcd_set_icon_mask()`.

On 2026-10-09 four RAM[6] addresses were corrected against that map: MIN
(bit 23), MAX (bit 22), mid dot (bit 15) and TIME (bit 26). Before the fix the
MAX icon lit the mid dot (the extra decimal point) and MIN/TIME lit nothing.
`tests/lcd_segment_map_vectors.py` checks the addresses in the built image.

## Default user cards

Short START/STOP button presses cycle visible cards backward/forward:

0. `TEMP` - current or last temperature, `TEMP` + `deg C`
1. `STATE` - `rdY`, `run`, or `StP`
2. `MIN` - minimum temperature, `TEMP` + `MIN` + `deg C`
3. `MAX` - maximum temperature, `TEMP` + `MAX` + `deg C`
4. `AVG` - average temperature, `TEMP` + `AVG` + `deg C`
5. `LOW` - low alarm threshold, down arrow + `MIN` + `deg C` (no `TEMP`)
6. `HIGH` - high alarm threshold, up arrow + `MAX` + `deg C` (no `TEMP`)
7. `TIME` - active/runtime value, hourglass + seconds/mins/hrs/days unit
8. `REC` - record count including events, `PROBE`
9. `INT` - measurement interval as a clock with colon, `TIME` + hourglass +
   `PROBE`: `M:SS` + `MIN(S)` below 20 minutes, `H:MM` + `HR(S)` below 20 hours,
   whole hours + `HR(S)` above
10. `BAT` - VDD-derived percent, battery + percent
11. `CLR` - clear report card

`LEDS` with the sun icon and `icon_scan` remain hidden technical cards. They
are not part of the default short-button cycle.

The HTML settings contain checkboxes for cards 0..11. `lcd_menu_mask` persists
these bits in configuration version 4; versions 1..3 migrate to all cards enabled.
Bit 0 (home/USB) is mandatory. Both navigation directions skip hidden cards; a
hidden startup card falls back to home. Selection does not change logging.
The old HTML startup labels and CONFIG.TXT legend did not match the firmware
enum; both now use the numbering above.

Numeric cards use only the existing LCD symbols: no introductory or periodic
text captions. Values stay visible throughout the awake period. Counts are
right-aligned without leading zeros, including zero itself. Counts and intervals
above 999 show 999 with the large arrow. The small arrows label the threshold
cards so they differ from the measured minimum/maximum cards. Menu order and the 10 s timeout are unchanged.

On USB the home screen remains `USB` with the large arrow. The battery card also
shows `USB`, without an ADC read or percent: USB-powered VDD is not a battery
charge reading. On battery the percentage remains an approximate VDD estimate.
An `Err` message has status icons only, without misleading temperature units.

## Hold actions

Long actions are confirmed by the button driver after 3 seconds. Before the
full 3 seconds pass, the current card remains visible.

After confirmation the LCD shows a nonblocking `5 -> 0` animation:

- START from `READY` or `PAUSE`: `r 5` .. `r 0`, then `run`
- STOP from `RUN`: `S 5` .. `S 0` with STOP icon, then `StP`
- CLR on `CLR` card: `C 5` .. `C 0`, then clear report and show `don`

Keep the initiating button(s) held through the countdown. Releasing or changing
the debounced button mask cancels at every digit, including zero before commit.
Cancellation restores the prior state and visible card without writing a START,
STOP or CLEAR operation. A real error is preserved, not replaced by the old state.
Remote commands cancel a pending local countdown before executing. CLEAR does
not erase data if the preceding STOP fails and does not show success on failure.

Holding START while already in `RUN` does not restart the session. Holding STOP
while in `READY` or `PAUSE` does not change state.

## Global indicators

- check mark: steady global OK indicator when there is no active alarm/error
- heart: short pulse after a successful stored measurement
- X: short blink matching the red LED when alarm/error is active
- red LED: 100 ms alarm/error pulse, about 1 s period
- green LED: pulses with the heart after a successful stored measurement
- STOP: shown across the normal menu when logging is not running, not just on STATE

The idle screen preserves CHECK/X, alarm direction arrows, the measurement
heartbeat and STOP. On a threshold card an arrow next to `MIN`/`MAX` is the card
label, not an alarm.

## Runtime display

Runtime uses the numeric field and one unit icon:

- below 1 minute: seconds, hourglass icon only
- below 1 hour: minutes + `MIN(S)`
- below 1000 hours: hours + `HR(S)`
- otherwise: days + `DAY(S)`, capped to 999

The logger stores active runtime separately from the total report span. The LCD
uses active runtime, so paused time is excluded.

## Verification

`python tests/lcd_menu_vectors.py` runs the existing USB/sensor/config checks and
LCD assertions against the Release ARM image. It covers statistics, caption-free cards,
tick wraparound, status/units, USB battery handling, overflow, and real numeric
rendering with emulated LCD registers. It does not verify contrast, ghosting or
physical segment labels on the glass.

`python tests/lcd_actions_vectors.py` also covers cancellation at all digits,
successful full holds, error preservation, menu persistence/validation/navigation,
and the actual HTML export of record capacity. HTML shows remaining record slots
as a percentage and count in Settings and Device information, including events.
