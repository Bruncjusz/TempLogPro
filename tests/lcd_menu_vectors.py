"""Exercise LCD menu in the Release ARM image; imports run USB/sensor regressions.

These tests verify software outputs, not the physical glass segment mapping.
"""
import struct
from usb_modes_vectors import Device, symbols

TEMP, CELSIUS = 0x80, 2
MIN, MAX, AVG = 0x800, 0x400, 0x200
STOP, CHECK, BIG, UP, DOWN = 0x8000, 0x10000, 0x20000, 0x40000, 0x80000
PROBE, TIME = 0x100, 0x1000
checks = 0


def check(condition):
    global checks
    checks += 1
    assert condition, f'LCD check {checks} failed'


class Display(Device):
    def __init__(self, usb=False):
        super().__init__()
        self.config(usb=usb)
        self.mask, self.frame, self.vdd_reads = 0, None, 0
        self.set('g_lcd_awake', 1)
        self.set('g_have_last_measurement', 1)
        self.set('g_last_temperature_x256', 29 * 256)
        self.uc.mem_write(symbols['g_cached_stats'], struct.pack(
            '<IIiiiIIIIIH2x', 1, 10, 26*256, 31*256, 28*256, 1, 100, 99, 45, 99, 0))
        hooks = {
            'templogpro_hw_lcd_set_icon_mask': self.icons,
            'templogpro_hw_lcd_show_text3': self.text,
            'templogpro_hw_lcd_show_value': self.value,
            'templogpro_hw_lcd_show_temperature_x256': self.temperature_frame,
            'templogpro_power_read_vdd_mv': self.vdd,
            'templogpro_logger_get_info': self.info,
        }
        for name, callback in hooks.items():
            self.addresses[symbols[name]] = callback

    def icons(self, mask, b, c): self.mask = mask
    def text(self, ptr, b, c): self.frame = ('text', bytes(self.uc.mem_read(ptr, 3)))
    def value(self, value, b, c): self.frame = ('value', value)
    def temperature_frame(self, value, b, c): self.frame = ('temperature', value)

    def info(self, ptr, b, c):
        values = [0]*14
        values[4], values[10] = 1234, 45
        self.uc.mem_write(ptr, struct.pack('<14I', *values))
        return 1

    def vdd(self, ptr, b, c):
        self.vdd_reads += 1
        self.uc.mem_write(ptr, struct.pack('<H', 2800))
        return 1

    def show(self, page, elapsed=1000, origin=0):
        self.set('g_lcd_view', page)
        self.set('g_last_user_activity_ms', origin)
        self.tick = (origin + elapsed) & 0xFFFFFFFF
        self.call('app_update_lcd')
        return self.frame


d = Display()
for page, value, icon in ((0, 29, 0), (2, 26, MIN), (3, 31, MAX), (4, 28, AVG)):
    check(d.show(page) == ('temperature', value*256))
    check(d.mask == CHECK | TEMP | CELSIUS | icon)

for page, value in ((5, 24), (6, 32)):
    for origin in (0, 0xFFFFFF00):
        for elapsed in (0, 749, 750, 1000, 4000):
            check(d.show(page, elapsed, origin) == ('temperature', value*256))
            # Thresholds add PROBE to the TEMP MIN/MAX of the measured extremes.
            check(d.mask == CHECK | CELSIUS | PROBE | TEMP | (MIN if page == 5 else MAX))

for page, value, icons in ((7, 45, 0x4000), (8, 999, 0x100 | BIG),
                           (10, 80, 0x2004)):
    for elapsed in (0, 750, 1000, 4000):
        check(d.show(page, elapsed) == ('value', value))
        check(d.mask == CHECK | icons)

# Interval card: plain seconds with PROBE + TIME, no unit icon (max 600 s).
for seconds in (1, 5, 10, 60, 599, 600):
    d.set('g_logging_interval_seconds', seconds)
    for elapsed in (0, 750, 4000):
        check(d.show(9, elapsed) == ('value', seconds))
        check(d.mask == CHECK | PROBE | TIME)
check(not d.mask & (UP | DOWN | BIG))

# Fahrenheit: every temperature card converts and shows the F unit instead of C.
FAHRENHEIT = 0x400000
def to_f_x256(c_x256):
    scaled = c_x256 * 9
    return int((scaled + 2) / 5 if scaled >= 0 else (scaled - 2) / 5) + 32 * 256
d.uc.mem_write(symbols['g_app_config'] + 36, struct.pack('<I', 1))
for page, celsius in ((0, 29), (2, 26), (3, 31), (4, 28), (5, 24), (6, 32)):
    check(d.show(page) == ('temperature', to_f_x256(celsius * 256)))
    check(d.mask & FAHRENHEIT and not d.mask & CELSIUS)
d.uc.mem_write(symbols['g_app_config'] + 36, struct.pack('<I', 0))
check(d.show(0) == ('temperature', 29 * 256) and d.mask & CELSIUS and not d.mask & FAHRENHEIT)

# Menu order: temperature, clock, state, AVG, then each measured extreme beside its threshold.
sequence = tuple(d.uc.mem_read(symbols['LCD_MENU_SEQUENCE'], 15))  # arm-none-eabi: 1-byte enums
check(sequence == (0, 12, 15, 16, 1, 4, 3, 6, 2, 5, 7, 8, 9, 10, 11))
# LED card: SUN + PROBE + On/OFF from led_enable (config() sets LEDs off).
check(d.show(15) == ('text', b'OFF') and d.mask & 0x00200100 == 0x00200100)
check(d.show(8) == ('value', 999) and d.mask & BIG)
check(d.show(7) == ('value', 45) and d.mask & 0x4000)
check(d.show(10) == ('value', 80) and d.mask & 0x2004 == 0x2004)

check(d.show(1) == ('text', b'run') and d.mask & PROBE and not d.mask & STOP)
d.set('g_app_state', 2)  # PAUSE
check(d.show(1) == ('text', b'StP') and d.mask & STOP and d.mask & PROBE)
check(d.show(0)[0] == 'temperature' and d.mask & STOP)
d.set('g_app_state', 6)  # ERROR (see templogpro_app.h)
check(d.show(5) == ('text', b'Err') and not d.mask & (TEMP | CELSIUS | UP | DOWN))
d.set('g_lcd_awake', 0)
check(d.show(9, 0) == ('text', b'   '))

d = Display(usb=True)
check(d.show(0) == ('text', b'USB') and d.mask == BIG)
check(d.show(10, 0) == ('text', b'USB') and not d.mask & 0x2004)
check(d.vdd_reads == 0)

# Exercise the real numeric renderer against emulated LCD registers.
d = Device()
d.uc.mem_map(0x40002000, 0x1000)
del d.addresses[symbols['templogpro_hw_lcd_show_value']]
digits = ((24, 15), (12, 6), (5, 23))
for value, active in ((0, (False, False, True)), (5, (False, False, True)),
                      (10, (False, True, True)), (100, (True, True, True)),
                      (999, (True, True, True)), (5, (False, False, True))):
    d.uc.mem_write(0x40002408, b'\x00'*4)  # No pending LCD RAM update.
    d.call('templogpro_hw_lcd_show_value', value)
    ram = struct.unpack('<16I', d.uc.mem_read(0x40002414, 64))
    for (ad, eg), expected in zip(digits, active):
        visible = any(ram[i] & (1 << ad) for i in (0, 2, 4, 6))
        visible |= any(ram[i] & (1 << eg) for i in (0, 2, 4))
        check(bool(visible) == expected)

# 12-hour clock: leading "1" (RAM0 bit 25) only for 10-12, colon (RAM4 bit 14) blinks.
d.addresses.pop(symbols['templogpro_hw_lcd_show_clock12'], None)
for hours, minutes, colon in ((12, 5, 1), (9, 30, 0), (1, 0, 1)):
    d.uc.mem_write(0x40002408, b'\x00'*4)
    d.call('templogpro_hw_lcd_show_clock12', hours, minutes, colon)
    ram = struct.unpack('<16I', d.uc.mem_read(0x40002414, 64))
    check(bool(ram[0] & (1 << 25)) == (hours >= 10))
    check(bool(ram[4] & (1 << 14)) == bool(colon))
    tens = any(ram[i] & (1 << 12) for i in (0, 2, 4, 6)) or any(ram[i] & (1 << 6) for i in (0, 2, 4))
    check(tens)  # minutes always show two digits, "9:30" and "1:00" alike

print(f'LCD menu: {checks} checks passed (physical mapping not verified).')
