"""Exercise the built diagnostic ARM image, including its real CDC parser.

No hardware/power conclusions: this tests read-only command isolation and bounds.
"""
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/test_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC, UC_ARM_REG_XPSR

BUILD = ROOT / "diagnostics/module_probe/build"
nm = sorted(Path("C:/ST").glob("STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-nm.exe"))[-1]
listing = subprocess.check_output([str(nm), "-n", str(BUILD / "TempLogPro_module_probe.elf")], text=True)
symbols = {p[2]: int(p[0], 16) for line in listing.splitlines() if len(p := line.split()) == 3}
image = (BUILD / "TempLogPro_module_probe.bin").read_bytes()
machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
machine.mem_map(0x08000000, 0x40000)
machine.mem_map(0x20000000, 0x8000)
machine.mem_write(0x08000000, image)
CALLBACK, RETURN, ARG, OUT = 0x0803FFE0, 0x0803FFF0, 0x20006000, 0x20007000
machine.mem_write(CALLBACK, b"\x70\x47")  # bx lr
responses = []
checks = 0
flash_status = 0
flash_id = b"\xef\x30\x13"


def read_string(pointer):
    result = bytearray()
    for i in range(2048):
        c = machine.mem_read(pointer + i, 1)[0]
        if c == 0:
            return result.decode("ascii")
        result.append(c)
    raise AssertionError("Unterminated response")


def hook(uc, address, size, user):
    if address == CALLBACK:
        responses.append(read_string(uc.reg_read(UC_ARM_REG_R0)))
    elif address in flash_hooks:
        name = flash_hooks[address]
        result = 1
        if name.startswith("w25x40_read_jedec_id"):
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0), flash_id)
            result = flash_status
        elif name == "templogpro_hw_flash_read":
            uc.mem_write(uc.reg_read(UC_ARM_REG_R1), bytes(range(256)))
        uc.reg_write(UC_ARM_REG_R0, result)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


flash_hooks = {address: name for name, address in symbols.items()
               if name in ("w25x40_init", "w25x40_power_down", "templogpro_hw_flash_read")
               or name.startswith("w25x40_read_jedec_id")}
machine.hook_add(UC_HOOK_CODE, hook)


def call(name, a0, a1=0):
    machine.reg_write(UC_ARM_REG_SP, 0x20007FF8)
    machine.reg_write(UC_ARM_REG_LR, RETURN | 1)
    machine.reg_write(UC_ARM_REG_XPSR, 0x01000000)
    machine.reg_write(UC_ARM_REG_R0, a0)
    machine.reg_write(UC_ARM_REG_R1, a1)
    machine.emu_start(symbols[name] | 1, RETURN, count=500000)
    assert machine.reg_read(UC_ARM_REG_PC) == RETURN, "Execution budget exhausted"
    return machine.reg_read(UC_ARM_REG_R0)


def check(ok):
    global checks
    checks += 1
    assert ok, f"Check {checks} failed"


for value in ("000000", "000100", "07FF00", "07ff00"):
    machine.mem_write(ARG, value.encode() + b"\0")
    check(call("probe_flash_address", ARG, OUT) == 1)
    check(struct.unpack("<I", machine.mem_read(OUT, 4))[0] == int(value, 16))
for value in ("", "0", "00000", "0000000", "000001", "07FFFF", "080000", "FFFFFF", "-00100", "0x0100", "00Z000", "000000 "):
    machine.mem_write(ARG, value.encode() + b"\0")
    check(call("probe_flash_address", ARG, OUT) == 0)

for cmd in ("PING", "HELP", "LOGGER START", "LOGGER CLEAR", "LOGGER STOP", "CONFIG BEGIN", "CONFIG END",
            "SET TIME 2026-09-19", "FLASH WRITE 000000", "FLASH READ 080000", "FLASH READ 000001", "GET GPIO F"):
    responses.clear()
    machine.mem_write(ARG, cmd.encode() + b"\0")
    call("probe_command", ARG, CALLBACK | 1)
    response = "".join(responses)
    check(response.startswith("BEGIN\r\n") and response.endswith("END\r\n"))
    check(("ERROR" not in response) == (cmd in ("PING", "HELP")))

for flash_status, flash_id, success in ((0, b"\xef\x30\x13", True),
                                      (2, b"\xef\x30\x13", False),
                                      (0, b"\xff\xff\xff", False)):
    machine.mem_write(symbols["probe_flash_ready"], b"\0" * 4)
    responses.clear()
    machine.mem_write(ARG, b"FLASH READ 07FF00\0")
    call("probe_command", ARG, CALLBACK | 1)
    response = "".join(responses)
    check(("ERROR" not in response) == success)
    if success:
        check(bytes(range(256)).hex().upper() in response)


def reset_cdc():
    for name in ("cdc_rx_head", "cdc_rx_tail", "cdc_tx_head", "cdc_tx_tail", "cdc_line_len"):
        machine.mem_write(symbols[name], b"\0\0")
    for name in ("cdc_config_overflow", "cdc_tx_busy"):
        machine.mem_write(symbols[name], b"\0")


def cdc_input(data):
    check(len(data) < 256)
    machine.mem_write(symbols["cdc_rx_queue"], data)
    machine.mem_write(symbols["cdc_rx_tail"], b"\0\0")
    machine.mem_write(symbols["cdc_rx_head"], struct.pack("<H", len(data)))
    call("USBD_MSC_CDC_Process", 0)
    length = struct.unpack("<H", machine.mem_read(symbols["cdc_tx_head"], 2))[0]
    return bytes(machine.mem_read(symbols["cdc_tx_queue"], length)).decode("ascii")


for data in (b"PING\n", b"LOGGER CLEAR\n", b"PING\0ignored\n", b"x" * 128 + b"PING\n"):
    reset_cdc()
    response = cdc_input(data)
    check(("TEMPLOGPRO_MODULE_PROBE_V2" in response) == (data == b"PING\n"))
    check(response.endswith("END\r\n"))
reset_cdc()
check(cdc_input(b"PI") == "")
check("TEMPLOGPRO_MODULE_PROBE_V2" in cdc_input(b"NG\n"))
print(f"PASS module_probe: {checks} checks on built ARM binary")
