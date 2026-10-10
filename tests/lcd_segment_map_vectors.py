"""Check physical LCD RAM bits written by the built image for each icon,
against the TempTale Ultra LCD RAM Map (temptale-ultra_LCD.pdf)."""
import struct, sys
from usb_modes_vectors import Device, symbols, ARG

MAP = {  # icon mask -> {ram word: bit} per the PDF map
    0x00000800: ('MIN',  {6: 23}),
    0x00000400: ('MAX',  {6: 22}),
    0x00000200: ('AVG',  {6: 13}),
    0x00001000: ('TIME', {6: 26}),
    0x00000080: ('TEMP', {2: 14}),
    0x00000100: ('PROBE',{2: 27}),
    0x00000004: ('%',    {0: 1}),
    0x00000008: ('RH',   {2: 1}),
    0x00000010: ('HR(S)',{4: 1}),
    0x00000020: ('MIN(S)',{6: 1}),
    0x00000040: ('DAY(S)',{4: 25}),
    0x00002000: ('Battery',{4: 26}),
    0x00004000: ('hourglass',{4: 27}),
    0x00008000: ('Stop',{4: 22}),
    0x00020000: ('big arrow',{0: 14}),
    0x00040000: ('up',{0: 22}),
    0x00080000: ('down',{2: 22}),
    0x00200000: ('sun',{0: 27}),
    0x00000001: ('heart',{0: 26}),
    0x00000002: ('degrees C',{0: 2, 4: 2}),
    0x00400000: ('degrees F',{0: 2, 2: 2}),
}
bad = 0
for mask, (name, expect) in MAP.items():
    d = Device()
    for n in ('templogpro_hw_lcd_set_icon_mask', 'templogpro_hw_lcd_show_text3'):
        del d.addresses[symbols[n]]
    d.uc.mem_map(0x40002000, 0x1000)
    d.uc.mem_write(ARG, b'   ')
    d.call('templogpro_hw_lcd_set_icon_mask', mask)
    d.call('templogpro_hw_lcd_show_text3', ARG)
    ram = struct.unpack('<16I', d.uc.mem_read(0x40002414, 64))
    got = {i: [b for b in range(32) if ram[i] >> b & 1] for i in range(16) if ram[i]}
    want = {k: [v] for k, v in sorted(expect.items())}
    ok = got == want
    bad += not ok
    print(f"{'OK ' if ok else 'BAD'} {name:10s} RAM bits {got}")

# Number renderer (interval card in seconds): digits per the map, no leading zeros.
SEVEN = {0: 0x3F, 1: 0x06, 2: 0x5B, 3: 0x4F, 4: 0x66, 5: 0x6D, 6: 0x7D, 7: 0x07, 8: 0x7F, 9: 0x6F}
DIGITS = ((24, 15), (12, 6), (5, 23))  # (A-D bit, E-G bit) for 100s, 10s, 1s
for value in (5, 60, 600, 999):
    d = Device()
    for n in ('templogpro_hw_lcd_set_icon_mask', 'templogpro_hw_lcd_show_value'):
        del d.addresses[symbols[n]]
    d.uc.mem_map(0x40002000, 0x1000)
    d.call('templogpro_hw_lcd_set_icon_mask', 0)
    d.call('templogpro_hw_lcd_show_value', value)
    ram = list(struct.unpack('<16I', d.uc.mem_read(0x40002414, 64)))
    want = [0] * 16
    shown = [value // 100 if value >= 100 else None,
             (value // 10) % 10 if value >= 10 else None,
             value % 10]
    for (ad, eg), digit in zip(DIGITS, shown):
        if digit is None:
            continue
        for seg, word in enumerate((0, 2, 4, 6)):
            if SEVEN[digit] >> seg & 1: want[word] |= 1 << ad
        for seg, word in enumerate((0, 2, 4)):
            if SEVEN[digit] >> (seg + 4) & 1: want[word] |= 1 << eg
    ok = ram == want
    bad += not ok
    print(f"{'OK ' if ok else 'BAD'} value {value}")
# Temperatures from 100.0 use the leading "1" (needed for Fahrenheit, e.g. 140.0 F).
for value, lead in ((140.0, True), (99.9, False), (-148.0, True)):
    d = Device()
    for n in ('templogpro_hw_lcd_set_icon_mask', 'templogpro_hw_lcd_show_temperature_x256'):
        del d.addresses[symbols[n]]
    d.uc.mem_map(0x40002000, 0x1000)
    d.call('templogpro_hw_lcd_set_icon_mask', 0)
    d.call('templogpro_hw_lcd_show_temperature_x256', int(round(value * 256)) & 0xFFFFFFFF, 0)
    ram = struct.unpack('<16I', d.uc.mem_read(0x40002414, 64))
    ok = bool(ram[0] >> 25 & 1) == lead and bool(ram[2] >> 26 & 1) == (value < 0)
    bad += not ok
    print(f"{'OK ' if ok else 'BAD'} temperature {value}")
print('RESULT', 'all icons match the map' if not bad else f'{bad} mismatches')
sys.exit(1 if bad else 0)
