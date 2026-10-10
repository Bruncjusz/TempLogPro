"""Run the diagnostic sensor experiment from the real ARM ELF with bus mocks.

Tests failure cleanup, not sensor identification or physical I2C timing.
"""
import struct
import module_probe_vectors as base
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC, UC_ARM_REG_LR


class Sensor:
    def __init__(self, fault=""):
        self.fault = fault
        self.cfg = 0 if fault == "wrong_baseline" else 0x60A0
        self.now = 0
        self.done_at = 0
        self.temp = 0x1A00
        self.converting = False
        self.writes = []

    def read(self, reg):
        self.now += 2
        if self.fault == "nack_before" and not self.writes:
            return None
        if self.converting and self.now >= self.done_at:
            self.temp += 0x10
            self.converting = False
        if reg == 1:
            return (self.cfg | 0x8000) if self.cfg & 0x100 and not self.converting else self.cfg & ~0x8000
        if reg == 0:
            return self.temp
        if reg == 2:
            return 0x4B10 if self.fault == "threshold_changed" and self.writes and self.writes[-1] == 0x60A0 else 0x4B00
        if reg == 3:
            return 0x5000
        raise AssertionError("Unexpected register read")

    def write(self, cfg):
        base.check(cfg in (0x60A0, 0x61A0, 0xE1A0))
        self.writes.append(cfg)
        if cfg == 0x60A0 and self.fault == "restore_nack":
            return 0
        self.cfg = cfg
        self.converting = cfg == 0xE1A0
        self.done_at = self.now + (1000 if self.fault == "timeout" else 11)
        if cfg == 0x61A0 and self.fault == "partial_shutdown":
            return 0
        if cfg == 0xE1A0 and self.fault == "oneshot_nack":
            return 0
        return 1


names = ("probe_sensor_read_word", "probe_sensor_write_config", "probe_sensor_tick", "probe_sensor_wait")
hooks = {base.symbols[name]: name for name in names}
sensor = None


def hook(uc, address, size, user):
    if address not in hooks:
        return
    name = hooks[address]
    arg = uc.reg_read(UC_ARM_REG_R0)
    result = 0
    if name == "probe_sensor_read_word":
        value = sensor.read(arg)
        if value is not None:
            uc.mem_write(uc.reg_read(UC_ARM_REG_R1), struct.pack("<H", value))
            result = 1
    elif name == "probe_sensor_write_config":
        result = sensor.write(arg)
    elif name == "probe_sensor_tick":
        result = sensor.now
    else:
        sensor.now += arg
    uc.reg_write(UC_ARM_REG_R0, result)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


base.machine.hook_add(UC_HOOK_CODE, hook)
for fault in ("", "wrong_baseline", "nack_before", "partial_shutdown", "oneshot_nack", "timeout", "restore_nack", "threshold_changed"):
    sensor = Sensor(fault)
    for name in ("recovery_pending", "recovery_last_ms"):
        base.machine.mem_write(base.symbols[name], b"\0" * 4)
    base.responses.clear()
    base.call("probe_sensor_test", base.CALLBACK | 1)
    reply = "".join(base.responses)
    base.check(("result=PASS" in reply) == (fault == ""))
    base.check(len(reply) < 950)
    if fault in ("wrong_baseline", "nack_before"):
        base.check(not sensor.writes)
    elif fault == "restore_nack":
        base.check("ERROR SENSOR_RESTORE_FAILED" in reply)
        base.check(sensor.cfg & 0x100)
        sensor.fault = ""
        sensor.now += 1000
        base.call("probe_sensor_recovery_poll", 0)
        base.check(sensor.cfg == 0x60A0)
        base.check(base.machine.mem_read(base.symbols["recovery_pending"], 4) == b"\0" * 4)
    else:
        base.check(sensor.cfg == 0x60A0)
        base.check("restored=1" in reply)
    if not fault:
        base.check("completed=4 busy_seen=4" in reply)
        base.check(sensor.writes == [0x61A0] + [0xE1A0] * 4 + [0x60A0])
    print(f"PASS sensor scenario: {fault or 'normal'}")

# Startup recovery after NRST interrupts a test with SD still set.
sensor = Sensor()
sensor.cfg = 0xE1A0
base.check(base.call("probe_sensor_recover", 0) == 1)
base.check(sensor.cfg == 0x60A0)
print(f"PASS module probe sensor suite: {base.checks} combined checks")
