"""Verify actual ARM flash wrappers mask only USB and restore its enable state."""
import struct
from usb_modes_vectors import Device, symbols, ARG
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PRIMASK

checks = 0


def check(value):
    global checks
    checks += 1
    assert value, f'flash guard check {checks} failed'


for enabled in (0, 1):
    for operation in ('read', 'write', 'erase_sector'):
        d = Device()
        d.uc.mem_map(0xE000E000, 4096)
        d.uc.mem_write(0xE000E100, struct.pack('<I', enabled << 20))
        writes = []
        d.uc.hook_add(UC_HOOK_MEM_WRITE, lambda u,a,addr,size,value,data: writes.append((addr,value)))
        del d.addresses[symbols['templogpro_hw_flash_' + operation]]

        def wake(a,b,c):
            check((0xE000E180, 1 << 20) in writes)
            check(d.get('g_hw_flash_busy') == 1)
            check(d.uc.reg_read(UC_ARM_REG_PRIMASK) == 0)
            return 0  # Every failure path must also restore USB.

        d.addresses[symbols['w25x40_wake']] = wake
        check(d.call('templogpro_hw_flash_' + operation,0x3000,ARG,12) == 0)
        check(d.get('g_hw_flash_busy') == 0)
        check(((0xE000E100,1<<20) in writes) == bool(enabled))

print(f'PASS flash USB guard: {checks} checks')
