"""ARM regressions for delayed start, delayed alarm, the low-battery icon and the
USB media-change signal, on the real Release image."""
import struct
from clock_off_vectors import ClockDevice, READY, RUN, PAUSE
from usb_modes_vectors import symbols, ARG, OUT

checks = 0
ICON_HRS, ICON_MINS, ICON_HOURGLASS, ICON_STOP = 0x10, 0x20, 0x4000, 0x8000
ICON_BATTERY, ICON_X, ICON_UP = 0x2000, 0x100000, 0x40000
ALARM_DELAY = 100  # TempLogProConfig.alarm_delay_min
FLAG_HIGH = 0x0010  # TEMPLOGPRO_LOG_FLAG_TEMP_HIGH


def check(condition):
    global checks
    checks += 1
    assert condition, f'start/alarm check {checks} failed'


def set_cfg(dev, offset, value):
    for name in ('g_config', 'g_app_config'):
        dev.uc.mem_write(symbols[name] + offset, struct.pack('<I', value))


# ---- delayed start ----
d = ClockDevice()
check(d.call('templogpro_app_schedule_start', d.ts - 5) == 0)          # past: refused
check(d.call('templogpro_app_schedule_start', d.ts + 600) == 1)
check(d.get('g_start_at') == d.ts + 600 and d.bkp.get(3) == d.ts + 600)
check(d.call('templogpro_app_stop_wake_seconds') == 1)                  # M:SS countdown
d.step(100)
check(d.clock == (10, 0, 1) and d.mask & ICON_HOURGLASS and d.mask & ICON_MINS)
d.ts += 590
d.step(500)
check(d.clock == (0, 10, 1) and d.starts == 0)
d.ts += 10
d.step(100)
check(d.starts == 1 and d.get('g_app_state') == RUN and d.get('g_start_at') == 0 and d.bkp.get(3) == 0)
check(d.call('templogpro_app_schedule_start', d.ts + 600) == 0)          # running: refused

d = ClockDevice(state=PAUSE)
check(d.call('templogpro_app_schedule_start', d.ts + 2 * 3600 + 30) == 1)
check(d.call('templogpro_app_stop_wake_seconds') == 30)                 # next minute of H:MM
d.step(100)
check(d.clock == (2, 0, 1) and d.mask & ICON_HRS and d.mask & ICON_HOURGLASS)
d.call('templogpro_app_stop_logging')                                    # STOP cancels the wait
check(d.get('g_start_at') == 0 and d.bkp.get(3) == 0 and d.starts == 0)

d = ClockDevice()
d.call('templogpro_app_schedule_start', d.ts + 30 * 3600)
d.step(100)
check(d.frame == ('value', 30) and d.mask & ICON_HRS)

# Hold STOP while waiting: countdown, then the wait is cancelled; logging never starts.
d = ClockDevice()
d.call('templogpro_app_schedule_start', d.ts + 3600)
queue = [4]  # BUTTON_STOP_LONG_CONFIRMED
d.addresses[symbols['templogpro_buttons_poll']] = lambda a, b, c: queue.pop(0) if queue else 0
d.held = 2
d.step(100)
for _ in range(6):
    d.step(400)
d.held = 0
d.step(100)
check(d.get('g_start_at') == 0 and d.starts == 0 and d.stops == 0)

# Weak battery when the start is due: retried a minute later, nothing started.
d = ClockDevice()
d.vdd_mv = 2500
d.addresses[symbols['templogpro_power_read_vdd_mv']] = d.read_vdd
d.call('templogpro_app_schedule_start', d.ts + 5)
d.ts += 5
d.step(100)
check(d.starts == 0 and d.get('g_start_at') == d.ts + 60)

# ---- delayed alarm ----
d = ClockDevice(state=RUN)
set_cfg(d, ALARM_DELAY, 10)
d.set('g_next_measurement_timestamp', d.ts + 100000)
hot = 33 * 256
d.call('app_store_measurement', hot, 1)
check(d.get('g_alarm_latched') == 0)
d.ts += 300
d.call('app_store_measurement', hot, 1)
check(d.get('g_alarm_latched') == 0)
check(d.show(1)[0] == 'text' and not d.mask & ICON_X)                  # no X before the delay
d.ts += 300
d.call('app_store_measurement', hot, 1)
check(d.get('g_alarm_latched') & FLAG_HIGH and d.bkp.get(4) == 0xA1A10000 | FLAG_HIGH)
check(d.show(1)[0] == 'text' and d.mask & ICON_X and d.mask & ICON_UP)

d = ClockDevice(state=RUN)
set_cfg(d, ALARM_DELAY, 10)
d.set('g_next_measurement_timestamp', d.ts + 100000)
for t, temp in ((0, hot), (300, 26 * 256), (400, hot), (900, hot)):     # broken by a return to range
    d.ts = 1000 + t
    d.call('app_store_measurement', temp, 1)
check(d.get('g_alarm_latched') == 0)
d.call('templogpro_app_clear_logger')
check(d.bkp.get(4) == 0xA1A10000)

d = ClockDevice(state=RUN)                                              # delay 0: immediate, as before
d.set('g_next_measurement_timestamp', d.ts + 100000)
d.call('app_store_measurement', hot, 1)
check(d.show(1)[0] == 'text' and d.mask & ICON_X)

# ---- low battery icon ----
d = ClockDevice()
d.addresses[symbols['templogpro_power_read_vdd_mv']] = lambda p, b, c: (d.uc.mem_write(p, struct.pack('<H', 2650)), 1)[1]
d.show(1)
check(d.mask & ICON_BATTERY)
d.addresses[symbols['templogpro_power_read_vdd_mv']] = lambda p, b, c: (d.uc.mem_write(p, struct.pack('<H', 2750)), 1)[1]
d.show(1)
check(not d.mask & ICON_BATTERY)
d = ClockDevice(usb=True)
d.addresses[symbols['templogpro_power_read_vdd_mv']] = lambda p, b, c: (d.uc.mem_write(p, struct.pack('<H', 2650)), 1)[1]
d.show(1)
check(not d.mask & ICON_BATTERY)

# ---- config: alarm_delay_min ----
d = ClockDevice()
check(d.call('templogpro_config_init') in (1, 2))
for text, ok, expected in ((b'alarm_delay_min=15\n', 1, 15), (b'alarm_delay_min=241\n', 0, 15), (b'alarm_delay_min=0\n', 1, 0)):
    d.uc.mem_write(ARG, text)
    check(d.call('templogpro_config_write_text', ARG, len(text)) == ok)
    check(d.call('templogpro_config_init') == 1)
    d.call('templogpro_config_get', OUT)
    check(struct.unpack('<I', d.uc.mem_read(OUT + ALARM_DELAY, 4))[0] == expected)

# ---- restart after a panel CLEAR ----
d = ClockDevice(usb=True)
d.call('templogpro_app_request_restart', 800)
d.step(500)
check(d.resets == [])
d.step(400)
check(len(d.resets) >= 1)

# ---- USB disk: media change after the files change ----
d = ClockDevice(usb=True)
check(d.call('storage_is_ready', 0) & 0xFF == 0)
d.call('templogpro_usb_files_snapshot_reset')
check(d.call('storage_is_ready', 0) & 0xFF != 0 and d.call('storage_is_ready', 0) & 0xFF != 0)
check(d.call('storage_is_ready', 0) & 0xFF == 0)
d.call('templogpro_usb_files_snapshot_reset')
d.call('storage_init', 0)                                               # a fresh mount signals nothing
check(d.call('storage_is_ready', 0) & 0xFF == 0)

print(f'PASS delayed start / delayed alarm / battery / media change: {checks} checks on actual Release ARM image')
