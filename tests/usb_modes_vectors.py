"""Run the actual Release ARM image with sensor, flash, RTC and UI hardware mocked.

Tests configuration migration/persistence, one-shot errors, and USB policy.
No claim about physical bus timing, USB enumeration or measured current.
"""
from pathlib import Path
import binascii
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tmp/test_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2

BIN = sorted(Path('C:/ST').glob('STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin'))[-1]
ELF = ROOT / 'Release/TempLogPro.elf'
subprocess.run([str(BIN / 'arm-none-eabi-objcopy.exe'), '-O', 'binary', str(ELF), str(ELF.with_suffix('.bin'))], check=True)
subprocess.run([str(BIN / 'arm-none-eabi-objcopy.exe'), '-O', 'ihex', str(ELF), str(ELF.with_suffix('.hex'))], check=True)
image = ELF.with_suffix('.bin').read_bytes()
listing = subprocess.check_output([str(BIN / 'arm-none-eabi-nm.exe'), '-n', str(ELF)], text=True)
symbols = {p[2]: int(p[0], 16) for line in listing.splitlines() if len(p := line.split()) == 3}
OUT, ARG, RETURN = 0x20007000, 0x20007400, 0x0803FFF0
checks = 0


def check(condition):
    global checks
    checks += 1
    assert condition, f'check {checks} failed'


class Device:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x08000000, 0x40000)
        self.uc.mem_map(0x20000000, 0x8000)
        self.uc.mem_write(0x08000000, image)
        size = symbols['_edata'] - symbols['_sdata']
        self.uc.mem_write(symbols['_sdata'], image[symbols['_sidata']-0x08000000:][:size])
        self.tick, self.ts = 1000, 100
        self.cfg, self.done_at = 0x60A0, 0
        self.temp, self.reads, self.writes, self.samples = 0x1A00, [], [], []
        self.starts = self.stops = 0
        self.fault = ''
        self.flash = bytearray(b'\xff' * 4096)
        self.flash_writes = 0
        self.vdd_mv, self.flash_ops = 3000, []
        self.handlers = {
            'HAL_GetTick': lambda a, b, c: self.tick,
            'templogpro_hw_sensor_init': lambda a, b, c: 1,
            'templogpro_hw_sensor_read_register': self.sensor_read,
            'templogpro_hw_sensor_write_config': self.sensor_write,
            'templogpro_hw_sensor_read_x256': self.temperature,
            'templogpro_hw_flash_init': lambda a, b, c: 1,
            'templogpro_hw_flash_read': self.flash_read,
            'templogpro_hw_flash_write': self.flash_write,
            'templogpro_hw_flash_erase_sector': self.flash_erase,
            'templogpro_rtc_get_timestamp': self.timestamp,
            'templogpro_logger_append': self.append,
            'templogpro_logger_stop': self.stop,
            'templogpro_logger_start': self.start,
            'templogpro_logger_resume': self.start,
            'templogpro_logger_stats_ready': lambda a, b, c: 1,
            'templogpro_power_read_vdd_mv': self.read_vdd,
            'templogpro_diag_flash_op_begin': lambda a, b, c: self.flash_ops.append(a),
            'templogpro_diag_flash_op_end': lambda a, b, c: self.flash_ops.append(0),
        }
        noop = ['templogpro_rtc_process', 'templogpro_rtc_take_event', 'templogpro_logger_process',
                'templogpro_logger_get_stats', 'templogpro_logger_get_info', 'templogpro_logger_set_interval',
                'templogpro_buttons_poll', 'templogpro_buttons_current_mask']
        noop += [name for name in symbols if name.startswith(('templogpro_hw_lcd_', 'templogpro_hw_led_'))]
        for name in noop:
            self.handlers[name] = lambda a, b, c: 0
        self.addresses = {symbols[k]: v for k, v in self.handlers.items() if k in symbols}
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def read_vdd(self, a, b, c):
        self.uc.mem_write(a, struct.pack('<H', self.vdd_mv))
        return 1

    def hook(self, uc, address, size, user):
        if address in self.addresses:
            value = self.addresses[address](*[uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)])
            uc.reg_write(UC_ARM_REG_R0, value or 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, a=0, b=0, c=0):
        self.uc.reg_write(UC_ARM_REG_SP, 0x20007FF8)
        self.uc.reg_write(UC_ARM_REG_LR, RETURN | 1)
        self.uc.reg_write(UC_ARM_REG_XPSR, 0x01000000)
        for r, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), (a,b,c)):
            self.uc.reg_write(r, value)
        self.uc.emu_start(symbols[name] | 1, RETURN, count=1000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == RETURN, name + ' did not return'
        return self.uc.reg_read(UC_ARM_REG_R0)

    def set(self, name, value): self.uc.mem_write(symbols[name], struct.pack('<I', value))
    def get(self, name): return struct.unpack('<I', self.uc.mem_read(symbols[name], 4))[0]
    def advance(self, ms): self.tick = (self.tick + ms) & 0xFFFFFFFF

    def sensor_read(self, reg, ptr, unused):
        self.reads.append(reg)
        if self.fault == 'nack': return 0
        assert reg == 1
        cfg = self.cfg
        if cfg & 0x100 and self.tick >= self.done_at and self.fault != 'timeout': cfg |= 0x8000
        else: cfg &= ~0x8000
        self.uc.mem_write(ptr, struct.pack('<H', cfg))
        return 1

    def sensor_write(self, cfg, b, c):
        check(cfg in (0x61A0, 0xE1A0))
        self.writes.append(cfg)
        self.cfg = cfg & ~0x8000
        self.done_at = self.tick + 27 if cfg & 0x8000 else self.tick
        return 0 if self.fault == 'partial_write' and cfg & 0x8000 else 1

    def temperature(self, ptr, b, c):
        if self.fault == 'temperature_nack': return 0
        self.temp += 16
        self.uc.mem_write(ptr, struct.pack('<i', self.temp))
        return 1

    def flash_read(self, address, ptr, size):
        self.uc.mem_write(ptr, bytes(self.flash[address & 4095:][:size]))
        return 1

    def flash_write(self, address, ptr, size):
        self.flash_writes += 1
        self.flash[address & 4095:(address & 4095)+size] = self.uc.mem_read(ptr,size)
        return 1

    def flash_erase(self, a,b,c):
        self.flash[:] = b'\xff'*4096
        return 1

    def timestamp(self, ptr,b,c):
        self.uc.mem_write(ptr,struct.pack('<I',self.ts))
        return 1

    def append(self, temp, flags,c):
        self.samples.append((temp,flags))
        return 1

    def stop(self,a,b,c): self.stops += 1; return 1
    def start(self,a,b,c): self.starts += 1; return 1

    def config(self, report=False, usb=True):
        values = struct.pack('<IiiIIIIII',10,24*256,32*256,1,0,1,0,int(not report),4095)
        for name in ('g_config','g_app_config'): self.uc.mem_write(symbols[name],values)
        self.set('g_config_valid',1)
        self.set('g_logging_interval_seconds',10)
        self.set('g_app_state',1)  # RUN
        self.set('g_app_mode',1)
        self.uc.mem_write(symbols['g_diag']+28,struct.pack('<I',int(usb)))
        self.uc.mem_write(symbols['g_diag']+12,struct.pack('<I',int(usb)))


# Migration keeps old limits/interval/LEDs and USB measurement behavior.
for version in (1,2,3,4):
    for mode in (0,1):
        d=Device()
        raw=bytearray(44)
        struct.pack_into('<IHHIIiiIIII',raw,0,0x54434647,version,0,7,10,24*256,32*256,1,0,1,0)
        if version>=3: struct.pack_into('<H',raw,40,mode)
        if version==4: struct.pack_into('<H',raw,6,5)
        crc_offset=38 if version==1 else 42
        struct.pack_into('<H',raw,crc_offset,binascii.crc_hqx(raw[:crc_offset],0xFFFF))
        d.flash[:44]=raw
        check(d.call('templogpro_config_init')==1)
        d.call('templogpro_config_get',OUT)
        cfg=struct.unpack('<IiiIIIIII',d.uc.mem_read(OUT,36))
        check(cfg[0:4]==(10,24*256,32*256,1))
        check(cfg[-2]==(mode if version>=3 else 1))
        # Older configurations gain the clock card (bit 12) added in config v7.
        check(cfg[-1]==((5 | 4096 | 8192 | 16384 | 32768 | 65536) if version==4 else 131071))
        check(d.flash_writes==0)
        check(d.call('templogpro_config_set',OUT)==1)
        check(d.call('templogpro_config_init')==1)
        d.call('templogpro_config_get',OUT)
        check(struct.unpack('<IiiIIIIII',d.uc.mem_read(OUT,36))==cfg)
        text=b'usb_logging=2\n'
        d.uc.mem_write(ARG,text)
        check(d.call('templogpro_config_write_text',ARG,len(text))==0)
        text=b'usb_logging=0\n'
        d.uc.mem_write(ARG,text)
        check(d.call('templogpro_config_write_text',ARG,len(text))==1)
        d.call('templogpro_config_read_text',0,OUT,512)
        check(b'usb_logging=0\r\n' in d.uc.mem_read(OUT,512))

# Interval is limited to 600 s (LCD shows plain seconds); older configs are clamped.
d=Device()
raw=bytearray(44)
struct.pack_into('<IHHIIiiIIII',raw,0,0x54434647,4,4095,7,86400,24*256,32*256,1,0,1,0)
struct.pack_into('<H',raw,40,1)
struct.pack_into('<H',raw,42,binascii.crc_hqx(raw[:42],0xFFFF))
d.flash[:44]=raw
check(d.call('templogpro_config_init')==1)
d.call('templogpro_config_get',OUT)
check(struct.unpack('<I',d.uc.mem_read(OUT,4))[0]==600)
for value,accepted in ((600,1),(601,0),(0,0),(1,1)):
    text=f'interval_seconds={value}\n'.encode()
    d.uc.mem_write(ARG,text)
    check(d.call('templogpro_config_write_text',ARG,len(text))==accepted)

# LCD temperature unit: C/F (or 0/1) in CONFIG.TXT, stored in config v5, rejected otherwise.
d=Device()
check(d.call('templogpro_config_init') in (1,2))
for text,accepted,unit in ((b'temperature_unit=F\n',1,1),(b'temperature_unit=c\n',1,0),
                           (b'temperature_unit=1\n',1,1),(b'temperature_unit=K\n',0,1)):
    d.uc.mem_write(ARG,text)
    check(d.call('templogpro_config_write_text',ARG,len(text))==accepted)
    check(d.call('templogpro_config_init')==1)
    d.call('templogpro_config_get',OUT)
    check(struct.unpack('<10I',d.uc.mem_read(OUT,40))[9]==unit)
d.call('templogpro_config_read_text',0,OUT,512)
check(b'temperature_unit=F\r\n' in d.uc.mem_read(OUT,512))

# Real sensor state machine, with the observed 27 ms single conversion.
for initial in (0x60A0,0xE1A0):
    d=Device(); d.cfg=initial
    check(d.call('templogpro_sensor_init')==1)
    check(d.cfg & 0x100)
    for _ in range(3):
        check(d.call('templogpro_sensor_request')==1)
        check(d.call('templogpro_sensor_pending')==1)
        reads=len(d.reads)
        check(d.call('templogpro_sensor_poll',OUT)==0)
        check(len(d.reads)==reads)
        d.advance(10); check(d.call('templogpro_sensor_poll',OUT)==0)
        d.advance(20); check(d.call('templogpro_sensor_poll',OUT)==1)
        check(d.call('templogpro_sensor_pending')==0 and d.cfg & 0x100)
        d.advance(10000)

for fault in ('partial_write','nack','timeout','temperature_nack','changed_config'):
    d=Device(); check(d.call('templogpro_sensor_init')==1)
    if fault=='partial_write': d.fault=fault
    started=d.call('templogpro_sensor_request')
    if fault=='partial_write': check(started==0)
    else:
        check(started==1)
        d.fault=fault
        if fault=='changed_config': d.cfg=0x6000
        d.advance(200 if fault=='timeout' else 30)
        check(d.call('templogpro_sensor_poll',OUT)==2)
    check(d.call('templogpro_sensor_pending')==0)
    check(d.get('g_sensor_initialized')==0)
    if fault!='changed_config': check(d.cfg & 0x100)
d=Device(); d.cfg=0x1234
check(d.call('templogpro_sensor_init')==0 and not d.writes)

# Application policy: USB logging, service STOP, unplug, explicit START.
for usb in (False,True):
    for report in (False,True):
        d=Device(); d.config(report,usb)
        check(d.call('templogpro_sensor_init')==1)
        d.call('templogpro_app_process')
        if usb and report:
            check(d.stops==1 and d.get('g_app_state')==2)
            check(not d.samples and not d.call('templogpro_sensor_pending'))
            check(d.call('templogpro_app_start_logging')==0 and not d.starts)
            d.call('templogpro_app_process'); check(d.stops==1)
            d.uc.mem_write(symbols['g_diag']+28,b'\0'*4)
            d.call('templogpro_app_process'); check(not d.starts and not d.samples)
            check(d.call('templogpro_app_start_logging')==1 and d.starts==1)
        else:
            check(d.call('templogpro_sensor_pending')==1)
            check(not d.samples)
            d.advance(30); d.call('templogpro_app_process')
            check(len(d.samples)==1 and d.samples[0][0]==0x1A10)
            d.advance(100); d.call('templogpro_app_process'); check(len(d.samples)==1)
            d.ts+=10; d.call('templogpro_app_process')
            check(d.call('templogpro_sensor_pending')==1)
            d.uc.mem_write(symbols['g_config']+28,b'\0'*4)
            d.uc.mem_write(symbols['g_diag']+28,b'\1\0\0\0')
            d.call('templogpro_app_apply_config')
            d.advance(30); d.call('templogpro_app_process')
            check(d.stops==1 and len(d.samples)==1 and d.cfg & 0x100)

# Counter rollover must not strand a conversion or publish a stale value.
d=Device(); d.call('templogpro_sensor_init'); d.tick=0xFFFFFFFA
check(d.call('templogpro_sensor_request')==1)
d.advance(30); d.done_at=0
check(d.call('templogpro_sensor_poll',OUT)==1)
print(f'PASS USB modes / sensor / config: {checks} checks on actual Release ARM image')
