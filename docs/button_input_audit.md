# Button input audit

Build date: 2026-08-05

## Previous button users

| Location | Previous use | Current status |
|---|---|---|
| `Src/main.c` `button_finder_mode()` | Direct GPIO scan/localizer with LCD output | Not part of normal source flow |
| `Src/main.c` `button_test_mode()` | Direct PA0/PA1 to LED live test | Not part of normal source flow |
| `Src/main.c` `settings_counter_mode()` | Old counter UI, direct PA0/PA1 actions and Winbond save | Not called by normal firmware |
| `Src/main.c` `button_pressed_event()` | Old press debounce helper | Removed from active code |
| `Src/templogpro_app.c` old hold countdown | Countdown while the button was still being held | Removed |
| `Src/templogpro_app.c` old wake suppression | Woke LCD immediately on stable press and could swallow long press | Removed |
| `Src/templogpro_app.c` old both-button fallback | Both buttons outside CLR opened CLR card | Removed |
| `Src/templogpro_app.c` app-local raw button state | Application interpreted physical state directly | Removed |

## Current button ownership

`Src/templogpro_buttons.c` is the only firmware module that configures and reads
PA0/PA1 as physical buttons.

The module outputs only these events:

- `BUTTON_START_SHORT`
- `BUTTON_STOP_SHORT`
- `BUTTON_START_LONG_CONFIRMED`
- `BUTTON_STOP_LONG_CONFIRMED`
- `BUTTON_BOTH_LONG_CONFIRMED`
- `BUTTON_PRESS_CANCELLED`

Timing:

- `BUTTON_DEBOUNCE_MS = 50`
- short press must be stable, released, and not longer than `700 ms`
- long press is confirmed after `3000 ms`
- after a long event, the driver waits for full stable release
- one physical press can emit at most one non-`NONE` event

## Menu mapping

| Event | Application action |
|---|---|
| `BUTTON_START_SHORT` while LCD is asleep | wake LCD only, consume event |
| `BUTTON_STOP_SHORT` while LCD is asleep | wake LCD only, consume event |
| `BUTTON_START_SHORT` while LCD is awake | previous visible card |
| `BUTTON_STOP_SHORT` while LCD is awake | next visible card |
| `BUTTON_START_LONG_CONFIRMED` in `READY` | nonblocking 5..0 animation, then START |
| `BUTTON_START_LONG_CONFIRMED` in `PAUSE` | nonblocking 5..0 animation, then RESUME |
| `BUTTON_START_LONG_CONFIRMED` in `RUN` | no action |
| `BUTTON_STOP_LONG_CONFIRMED` in `RUN` | nonblocking 5..0 animation, then PAUSE |
| `BUTTON_STOP_LONG_CONFIRMED` in `READY` or `PAUSE` | no action |
| `BUTTON_BOTH_LONG_CONFIRMED` on `CLR` card | nonblocking 5..0 animation, then clear report |
| `BUTTON_BOTH_LONG_CONFIRMED` on hidden `LEDS` card | toggle physical LEDs |
| `BUTTON_BOTH_LONG_CONFIRMED` on other cards | no action |
| `BUTTON_PRESS_CANCELLED` | no action |

The long-hold countdown is no longer shown before the full 3 seconds are
confirmed. Releasing earlier cancels the operation and does not navigate.

## Verification

Search after refactor showed no active direct PA0/PA1 action outside
`Src/templogpro_buttons.c`.

Remaining matches:

- `Src/templogpro_buttons.c`: GPIOA PA0/PA1 configuration, pull-ups, IDR read and FSM.
- `Src/main.c`: neutral `GPIOA_BASE` definitions and unrelated SPI1/USB/ADC uses.
- ST HAL files: generic EXTI/callback comments and library implementation.

Button vector tests:

```text
python tests\button_fsm_vectors.py
button_fsm_vectors: OK
```

Firmware build:

```text
text 77276
data   308
bss  12208
```
