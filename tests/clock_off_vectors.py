"""ARM regressions for clock mode (stopped logger shows H:MM) and OFF storage mode."""
import struct
from lcd_menu_vectors import Display
from usb_modes_vectors import symbols, ARG, OUT

checks = 0
ICON_TIME, ICON_SUN = 0x00001000, 0x00200000
CLOCK_MODE = symbols['g_app_config'] + 92  # TempLogProConfig.clock_mode (after tz_name)
READY, RUN, PAUSE = 0, 1, 2


def check(condition):
    global checks
    checks += 1
    assert condition, f'clock/off check {checks} failed'


class ClockDevice(Display):
    def __init__(self, usb=False, state=READY):
        super().__init__(usb=usb)
        self.set('g_app_state', state)
        self.now = (2026, 10, 15, 14, 37, 20)
        self.clock, self.suspends, self.inits, self.held = None, 0, 0, 0
        hooks = {
            'templogpro_rtc_get': self.rtc_get,
            'templogpro_hw_lcd_show_clock12': self.clock12,
            'templogpro_hw_lcd_suspend': self.suspend,
            'templogpro_hw_lcd_init': self.init,
            'templogpro_buttons_current_mask': lambda a, b, c: self.held,
        }
        for name, callback in hooks.items():
            self.addresses[symbols[name]] = callback

    def rtc_get(self, ptr, b, c):
        y, mo, d, h, mi, s = self.now
        self.uc.mem_write(ptr, struct.pack('<HBBBBB', y, mo, d, h, mi, s))
        return 1

    def clock12(self, h, m, colon): self.clock = (h, m, colon)
    def suspend(self, a, b, c): self.suspends += 1
    def init(self, a, b, c): self.inits += 1

    def usb(self, present):
        for offset in (12, 28):
            self.uc.mem_write(symbols['g_diag'] + offset, struct.pack('<I', int(present)))

    def step(self, ms=1000):
        self.advance(ms)
        self.call('templogpro_app_process')


# Clock mode off: a stopped logger sleeps 60 s with a blank display.
d = ClockDevice()
d.set('g_lcd_awake', 0)
check(d.call('templogpro_app_stop_wake_seconds') == 60)

# Clock mode on: wake on the next full minute and draw H:MM with a steady colon,
# TIME icon and the sun in the afternoon.
d.uc.mem_write(CLOCK_MODE, struct.pack('<I', 1))
check(d.call('templogpro_app_stop_wake_seconds') == 40)
d.set('g_last_user_activity_ms', 0)
d.step(11000)  # past the LCD timeout, display asleep
check(d.get('g_lcd_awake') == 0)
check(d.clock == (2, 37, 1) and d.mask == ICON_TIME | ICON_SUN)  # no X / STOP / check icons
d.set('g_app_state', PAUSE)  # paused logger: still a plain clock
d.now = (2026, 10, 15, 9, 5, 59)
d.clock = None
d.step(1000)
check(d.clock == (9, 5, 1) and d.mask == ICON_TIME)
check(d.call('templogpro_app_stop_wake_seconds') == 1)
d.set('g_app_state', RUN)
check(d.call('templogpro_app_stop_wake_seconds') == 1)  # RUN: 1 s measurement tick, no clock

# OFF: armed over USB stops a running logger, waits for unplug.
d = ClockDevice(usb=True, state=RUN)
check(d.call('templogpro_app_power_off_arm') == 1)
check(d.stops == 1 and d.get('g_app_state') == PAUSE and d.call('templogpro_app_power_off_armed') == 1)
d.step()
check(d.get('g_off_active') == 0)
d.usb(False)
d.step()
check(d.get('g_off_active') == 1 and d.get('g_off_armed') == 0 and d.suspends == 1)
check(d.call('templogpro_app_stop_wake_seconds') == 0)
check(d.call('templogpro_app_should_stop_sleep') == 1)
d.step(60000)
check(d.get('g_off_active') == 1 and d.starts == 0)

# Holding one button does nothing; both buttons: 3-2-1 countdown, then on.
d.held = 1
d.step()
check(d.get('g_action_active') == 0 and d.get('g_off_active') == 1)
d.held = 3
d.step(100)
check(d.inits == 1 and d.get('g_action_active') == 1 and d.get('g_action_digit') == 3)
check(d.call('templogpro_app_should_stop_sleep') == 0)
d.step()
d.step()
check(d.get('g_off_active') == 1 and d.get('g_action_digit') == 1)
d.step()
check(d.get('g_off_active') == 0 and d.get('g_action_active') == 0)
check(d.get('g_app_state') == PAUSE and d.get('g_lcd_awake') == 1 and d.starts == 0)

# Released before the end: back to OFF.
d = ClockDevice(usb=True)
d.call('templogpro_app_power_off_arm')
d.usb(False)
d.step()
d.held = 3
d.step(100)
d.step()
d.held = 0
d.step()
check(d.get('g_off_active') == 1 and d.get('g_action_active') == 0 and d.suspends == 2)

# Plugging USB in ends OFF.
d.usb(True)
d.step()
check(d.get('g_off_active') == 0 and d.inits == 2)

# Cancel over USB before unplugging.
d = ClockDevice(usb=True)
d.call('templogpro_app_power_off_arm')
d.call('templogpro_app_power_off_cancel')
d.usb(False)
d.step()
check(d.get('g_off_active') == 0)

# Clock mode while logging: the same plain clock, the heart only during a measurement pulse.
ICON_HEART = 0x00000001
d = ClockDevice(state=RUN)
d.uc.mem_write(CLOCK_MODE, struct.pack('<I', 1))
d.set('g_last_user_activity_ms', 0)
d.set('g_next_measurement_timestamp', d.ts + 100000)
d.step(11000)
check(d.get('g_lcd_awake') == 0 and d.clock == (2, 37, 1))
check(d.mask & ~ICON_HEART == ICON_TIME | ICON_SUN)

# OFF card in the LCD menu: both buttons, countdown, "OFF", then dark (battery).
APP_ACTION_OFF, LCD_VIEW_OFF = 5, 10  # OFF lives on the battery card
d = ClockDevice(state=RUN)
d.set('g_lcd_view', LCD_VIEW_OFF)
d.held = 3
d.call('app_start_action', APP_ACTION_OFF, d.tick)
for _ in range(6):
    d.step(400)
check(d.stops == 1 and d.get('g_app_state') == PAUSE and d.get('g_action_active') == 0)
check(d.frame == ('text', b'OFF') and d.get('g_off_active') == 0)
d.held = 0
d.step(1300)
check(d.get('g_off_active') == 1 and d.suspends == 1)

# Released during the countdown: nothing happens.
d = ClockDevice()
d.set('g_lcd_view', LCD_VIEW_OFF)
d.held = 3
d.call('app_start_action', APP_ACTION_OFF, d.tick)
d.step(400)
d.held = 0
d.step(400)
d.step(2000)
check(d.get('g_off_armed') == 0 and d.get('g_off_active') == 0)

# OFF card on USB: armed, dark only after unplugging.
d = ClockDevice(usb=True)
d.set('g_lcd_view', LCD_VIEW_OFF)
d.held = 3
d.call('app_start_action', APP_ACTION_OFF, d.tick)
for _ in range(6):
    d.step(400)
d.held = 0
d.step(2000)
check(d.get('g_off_armed') == 1 and d.get('g_off_active') == 0)
d.usb(False)
d.step()
check(d.get('g_off_active') == 1)

# Clock mode / LED cards: hold START = on, hold STOP = off, each after a 5..0
# countdown ("On5".."On0", "OF5".."OF0"); logging is not touched; release cancels.
BUTTON_START_LONG, BUTTON_STOP_LONG = 3, 4
LCD_VIEW_CLOCK_MODE, LCD_VIEW_LEDS = 12, 15  # clock mode lives on the clock card
LED_ENABLE = symbols['g_app_config'] + 24


LCD_VIEW_ECO, ECO_MODE = 16, symbols['g_app_config'] + 96


def option(dev, view):
    address = {LCD_VIEW_CLOCK_MODE: CLOCK_MODE, LCD_VIEW_LEDS: LED_ENABLE, LCD_VIEW_ECO: ECO_MODE}[view]
    return struct.unpack('<I', dev.uc.mem_read(address, 4))[0]


def hold(dev, queue, event, mask, steps=6):
    queue.append(event)
    dev.held = mask
    dev.step(100)
    first = dev.frame
    for _ in range(steps):
        dev.step(400)
    dev.held = 0
    dev.step(100)
    return first


for view, offset in ((LCD_VIEW_CLOCK_MODE, 92), (LCD_VIEW_LEDS, 24), (LCD_VIEW_ECO, 96)):
    for state in (READY, RUN):
        d = ClockDevice(state=state)
        check(d.call('templogpro_config_init') in (1, 2))
        queue = []
        d.addresses[symbols['templogpro_buttons_poll']] = lambda a, b, c, q=queue: q.pop(0) if q else 0
        d.set('g_lcd_view', view)
        first = hold(d, queue, BUTTON_START_LONG, 1)
        check(first == ('text', b'On5') and option(d, view) == 1)
        check(d.get('g_action_active') == 0 and d.starts == 0 and d.stops == 0 and d.get('g_app_state') == state)
        d.call('templogpro_config_get', OUT)
        check(struct.unpack('<I', d.uc.mem_read(OUT + offset, 4))[0] == 1)  # saved
        first = hold(d, queue, BUTTON_STOP_LONG, 2)
        check(first == ('text', b'OF5') and option(d, view) == 0)
        check(d.get('g_action_active') == 0 and d.starts == 0 and d.stops == 0 and d.get('g_app_state') == state)
        d.call('templogpro_config_get', OUT)
        check(struct.unpack('<I', d.uc.mem_read(OUT + offset, 4))[0] == 0)
        # Released half-way: nothing changes.
        hold(d, queue, BUTTON_START_LONG, 1, steps=2)
        check(option(d, view) == 0 and d.get('g_action_active') == 0 and d.get('g_lcd_view') & 0xFF == view)

# Weak battery: refused after the countdown, nothing written, "bAt".
d = ClockDevice()
d.call('templogpro_config_init')
d.vdd_mv = 2200  # VDD below the 2.3 V flash minimum
d.addresses[symbols['templogpro_power_read_vdd_mv']] = d.read_vdd
queue = []
d.addresses[symbols['templogpro_buttons_poll']] = lambda a, b, c: queue.pop(0) if queue else 0
d.set('g_lcd_view', LCD_VIEW_CLOCK_MODE)
writes = d.flash_writes
queue.append(BUTTON_START_LONG)
d.held = 1
d.step(100)
for _ in range(6):
    d.step(400)
check(option(d, LCD_VIEW_CLOCK_MODE) == 0 and d.flash_writes == writes)
check(d.get('g_lcd_message_icons') != 0 and d.frame == ('text', b'bAt'))
check(d.mask in (0, 0x00002000 | 0x00100000))  # only battery + X (blinking), no check mark
d.held = 0

# Eco: after the LCD timeout the controller is off, LEDs and heart too; a logging
# device wakes only for the next measurement; a button brings the display back.
for state in (READY, RUN):
    d = ClockDevice(state=state)
    d.uc.mem_write(ECO_MODE, struct.pack('<I', 1))
    d.uc.mem_write(CLOCK_MODE, struct.pack('<I', 1))  # eco wins over the clock
    d.uc.mem_write(LED_ENABLE, struct.pack('<I', 1))
    leds = []
    d.addresses[symbols['templogpro_hw_led_green_on']] = lambda a, b, c: leds.append('g')
    d.addresses[symbols['templogpro_hw_led_red_on']] = lambda a, b, c: leds.append('r')
    d.set('g_last_user_activity_ms', 0)
    d.set('g_next_measurement_timestamp', d.ts + 37)
    d.step(11000)
    check(d.get('g_lcd_awake') == 0 and d.get('g_lcd_hw_off') == 1 and d.suspends == 1 and d.clock is None)
    check(d.call('templogpro_app_should_stop_sleep') == 1)
    check(d.call('templogpro_app_stop_wake_seconds') == (37 if state == RUN else 60))
    leds.clear()
    d.set('g_last_measurement_ms', d.tick)  # a fresh measurement: no heartbeat in eco
    d.step(100)
    check(leds == [] and d.suspends == 1)
    queue = [1]  # BUTTON_START_SHORT wakes the display
    d.addresses[symbols['templogpro_buttons_poll']] = lambda a, b, c: queue.pop(0) if queue else 0
    d.step(100)
    check(d.get('g_lcd_awake') == 1 and d.get('g_lcd_hw_off') == 0 and d.inits == 1)
# Eco on USB: nothing is switched off.
d = ClockDevice(usb=True)
d.uc.mem_write(ECO_MODE, struct.pack('<I', 1))
d.set('g_last_user_activity_ms', 0)
d.step(11000)
check(d.get('g_lcd_hw_off') == 0 and d.suspends == 0)

# clock_mode survives CONFIG text -> flash -> decode; only 0/1 are accepted.
d = ClockDevice()
check(d.call('templogpro_config_init') in (1, 2))
for text, ok, expected in ((b'clock_mode=1\n', 1, 1), (b'clock_mode=2\n', 0, 1), (b'clock_mode=0\n', 1, 0)):
    d.uc.mem_write(ARG, text)
    check(d.call('templogpro_config_write_text', ARG, len(text)) == ok)
    d.call('templogpro_config_get', OUT)
    check(struct.unpack('<I', d.uc.mem_read(OUT + 92, 4))[0] == expected)
d.uc.mem_write(ARG, b'clock_mode=1\n')
d.call('templogpro_config_write_text', ARG, 13)
check(d.call('templogpro_config_init') == 1)
d.call('templogpro_config_get', OUT)
check(struct.unpack('<I', d.uc.mem_read(OUT + 92, 4))[0] == 1)

# eco_mode and menu bit 16 (stored in a spare byte) survive flash -> decode.
for text, mask, eco in ((b'lcd_menu_mask=131071\neco_mode=1\n', 131071, 1), (b'lcd_menu_mask=65537\neco_mode=0\n', 65537, 0)):
    d.uc.mem_write(ARG, text)
    check(d.call('templogpro_config_write_text', ARG, len(text)) == 1)
    check(d.call('templogpro_config_init') == 1)
    d.call('templogpro_config_get', OUT)
    check(struct.unpack('<I', d.uc.mem_read(OUT + 32, 4))[0] == mask)
    check(struct.unpack('<I', d.uc.mem_read(OUT + 96, 4))[0] == eco)

print(f'PASS clock mode / OFF: {checks} checks on actual Release ARM image')
