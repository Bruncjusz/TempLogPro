"""Power-loss tests for the Winbond log and configuration format.

Compiles the production templogpro_logger.c, templogpro_config.c and
templogpro_storage_layout.c together with tests/storage_harness.c for Cortex-M3
and runs them in unicorn on a simulated NOR flash. For every flash program or
erase operation of a workload, power is cut halfway through that operation;
the next boot must recover all acknowledged records, keep the log consistent,
keep a valid configuration and continue logging.

Requires ARM GCC (CubeIDE or ARM_GNU_BIN) and the PC-only unicorn package in
tmp/test_deps. This checks C logic on a NOR model, not the physical chip.
Run with --quick to test every 7th crash point only.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/test_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_XPSR

FLASH_BASE, FLASH_SIZE = 0x08000000, 0x20000
RAM_BASE, RAM_SIZE = 0x20000000, 0x10000
NOR_BASE, NOR_SIZE = 0x60000000, 512 * 1024
HALT = 0x0801FFF0

FIELDS = ["phase", "crash_at_op", "stuck_address", "samples", "do_format", "now",
          "expect_records", "expect_samples", "expect_config", "legacy_count", "loose",
          "ops", "crashed", "acked_records", "acked_samples", "config_stage", "format_done",
          "final_records", "final_state", "meta_writes", "last_error"]

PHASE_WORKLOAD, PHASE_VERIFY, PHASE_LEGACY = 1, 2, 3
SAMPLES = 760            # crosses two record sectors (341 slots each)
RECORDS_START = 0x3000
RECORDS_PER_SECTOR = 4096 // 12
LOGGING = 1


def find_gcc():
    if os.environ.get("ARM_GNU_BIN"):
        return Path(os.environ["ARM_GNU_BIN"]) / "arm-none-eabi-gcc.exe"
    gcc = shutil.which("arm-none-eabi-gcc")
    if gcc:
        return Path(gcc)
    candidates = sorted(Path("C:/ST").glob("STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe"))
    if not candidates:
        raise RuntimeError("Set ARM_GNU_BIN to your ARM GCC bin directory")
    return candidates[-1]


def build():
    gcc = find_gcc()
    suffix = ".exe" if os.name == "nt" else ""
    out = ROOT / "tmp/storage_tests"
    out.mkdir(parents=True, exist_ok=True)
    linker = out / "test.ld"
    linker.write_text(f"""MEMORY {{
 FLASH (rx) : ORIGIN = {FLASH_BASE:#x}, LENGTH = {FLASH_SIZE - 0x100:#x}
 RAM (rwx) : ORIGIN = {RAM_BASE:#x}, LENGTH = {RAM_SIZE:#x}
}}
SECTIONS {{
 .text : {{ *(.text*) *(.rodata*) }} > FLASH
 .data : {{ __data_start__ = .; *(.data*) __data_end__ = .; }} > RAM AT> FLASH
 .bss (NOLOAD) : {{ __bss_start__ = .; *(.bss*) *(COMMON) __bss_end__ = .; }} > RAM
 /DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) }}
}}
""", encoding="ascii")
    elf, binary = out / "storage.elf", out / "storage.bin"
    sources = ["tests/storage_harness.c", "Src/templogpro_logger.c",
               "Src/templogpro_config.c", "Src/templogpro_storage_layout.c"]
    command = [str(gcc), "-mcpu=cortex-m3", "-mthumb", "-std=gnu11", "-Os",
               "-Wall", "-Werror", "-ffreestanding", "-nostdlib", "-fno-builtin",
               "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
               f"-I{ROOT / 'Inc'}", *[str(ROOT / s) for s in sources],
               f"-T{linker}", "-Wl,-e,run_tests", "-lgcc", "-o", str(elf)]
    subprocess.run(command, check=True)
    subprocess.run([str(gcc.parent / ("arm-none-eabi-objcopy" + suffix)), "-O", "binary",
                    "-j", ".text", str(elf), str(binary)], check=True)
    symbols = subprocess.check_output([str(gcc.parent / ("arm-none-eabi-nm" + suffix)), "-n", str(elf)], text=True)
    addresses = {parts[2]: int(parts[0], 16) for line in symbols.splitlines()
                 if len(parts := line.split()) == 3}
    if addresses["__data_start__"] != addresses["__data_end__"]:
        raise RuntimeError("harness expects no initialised .data")
    return binary.read_bytes(), addresses


class Device:
    def __init__(self, image, addresses):
        self.addresses = addresses
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(FLASH_BASE, FLASH_SIZE)
        self.uc.mem_map(RAM_BASE, RAM_SIZE)
        self.uc.mem_map(NOR_BASE, NOR_SIZE)
        self.uc.mem_write(FLASH_BASE, image)
        self.erase_nor()

    def erase_nor(self):
        self.uc.mem_write(NOR_BASE, b"\xff" * NOR_SIZE)

    def nor(self):
        return bytes(self.uc.mem_read(NOR_BASE, NOR_SIZE))

    def set_nor(self, data):
        self.uc.mem_write(NOR_BASE, data)

    def reset(self):
        """Power cycle: RAM is lost, NOR keeps whatever was programmed."""
        start, end = self.addresses["__bss_start__"], self.addresses["__bss_end__"]
        self.uc.mem_write(start, b"\x00" * (end - start))

    def set(self, **values):
        for name, value in values.items():
            self.uc.mem_write(self.addresses["ctl"] + 4 * FIELDS.index(name), struct.pack("<I", value))

    def get(self, name):
        return struct.unpack("<I", self.uc.mem_read(self.addresses["ctl"] + 4 * FIELDS.index(name), 4))[0]

    def run(self, phase, **values):
        self.set(phase=phase, ops=0, crashed=0, **values)
        self.uc.reg_write(UC_ARM_REG_SP, RAM_BASE + RAM_SIZE - 8)
        self.uc.reg_write(UC_ARM_REG_LR, HALT | 1)
        self.uc.reg_write(UC_ARM_REG_XPSR, 0x01000000)
        self.uc.emu_start(self.addresses["run_tests"] | 1, HALT, count=400_000_000)
        if self.uc.reg_read(UC_ARM_REG_PC) != HALT:
            raise AssertionError("execution limit reached")
        if self.get("crashed"):
            return None
        return self.uc.reg_read(UC_ARM_REG_R0)


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def record_bytes(timestamp, temp, flags, sequence):
    body = struct.pack("<IhHH", timestamp, temp, flags, sequence)
    return body + struct.pack("<H", crc16(body))


def record_address(index):
    return RECORDS_START + (index // RECORDS_PER_SECTOR) * 4096 + (index % RECORDS_PER_SECTOR) * 12


def v2_metadata(count, generation):
    raw = bytearray(64)
    struct.pack_into("<IHHIIIIIIII", raw, 0, 0x544C4F47, 2, 0, generation, 1, count,
                     record_address(count), RECORDS_START, 10, 1000, 0)
    struct.pack_into("<HHIII", raw, 40, LOGGING, 0, 0, 1000, 1000 + count * 10)
    struct.pack_into("<H", raw, 62, crc16(raw[:62]))
    return bytes(raw)


def accepted_intervals(stage):
    return {0: 0, 1: (1 << 11) | (1 << 22), 2: 1 << 22}[stage]


def crash_sweep(device, label, base_nor, do_format, step):
    device.set_nor(base_nor)
    device.reset()
    result = device.run(PHASE_WORKLOAD, crash_at_op=0, stuck_address=0, samples=SAMPLES,
                        do_format=do_format, now=50_000, acked_records=0, acked_samples=0,
                        config_stage=0, format_done=0, meta_writes=0)
    if result != 0:
        raise AssertionError(f"{label}: workload failed at harness line {result}")
    total_ops = device.get("ops")
    meta_writes = device.get("meta_writes")
    if meta_writes > 40:
        raise AssertionError(f"{label}: {meta_writes} metadata writes for {SAMPLES} samples")

    checked = 0
    for crash in range(1, total_ops + 1, step):
        device.set_nor(base_nor)
        device.reset()
        result = device.run(PHASE_WORKLOAD, crash_at_op=crash, stuck_address=0, samples=SAMPLES,
                            do_format=do_format, now=50_000, acked_records=0, acked_samples=0,
                            config_stage=0, format_done=0)
        if result is not None:
            raise AssertionError(f"{label}: crash point {crash} not reached (result {result})")
        acked_records = device.get("acked_records")
        acked_samples = device.get("acked_samples")
        stage = device.get("config_stage")
        loose = 1 if (do_format and not device.get("format_done")) else 0
        now = device.get("now")
        device.reset()
        result = device.run(PHASE_VERIFY, crash_at_op=0, stuck_address=0, now=now + 100,
                            expect_records=acked_records, expect_samples=acked_samples,
                            expect_config=accepted_intervals(stage), loose=loose)
        if result != 0:
            raise AssertionError(f"{label}: power loss at flash op {crash} -> verify failed at "
                                 f"harness line {result} (acked {acked_records} slots, "
                                 f"{acked_samples} samples)")
        checked += 1
    print(f"PASS {label}: {checked} power-loss points of {total_ops} flash ops, "
          f"{meta_writes} metadata writes for {SAMPLES} samples")


def stuck_slot(device):
    """A slot that never programs is zeroed and skipped; logging continues."""
    device.erase_nor()
    device.reset()
    bad = record_address(200)
    result = device.run(PHASE_WORKLOAD, crash_at_op=0, stuck_address=bad, samples=400,
                        do_format=0, now=50_000, acked_records=0, acked_samples=0,
                        config_stage=0, format_done=0)
    if result != 0:
        raise AssertionError(f"stuck slot: workload failed at harness line {result}")
    records = device.get("final_records")
    device.reset()
    result = device.run(PHASE_VERIFY, crash_at_op=0, stuck_address=bad, now=device.get("now") + 100,
                        expect_records=records, expect_samples=400, expect_config=1 << 22, loose=0)
    if result != 0:
        raise AssertionError(f"stuck slot: verify failed at harness line {result}")
    print(f"PASS stuck slot: logging continued past a slot that never programs ({records} slots)")


def legacy_err_slot(device):
    """Reproduces the September Err state left by the v2 firmware."""
    count = 100
    nor = bytearray(b"\xff" * NOR_SIZE)
    for index in range(count):
        address = record_address(index)
        nor[address:address + 12] = record_bytes(1000 + index * 10, 5 * 256, 1, index + 1)
    partial = record_bytes(1000 + count * 10, 5 * 256, 1, count + 1)
    address = record_address(count)
    nor[address:address + 6] = partial[:6]          # write interrupted before metadata
    nor[0x1000:0x1040] = v2_metadata(count, generation=500)
    device.set_nor(bytes(nor))
    device.reset()
    result = device.run(PHASE_LEGACY, legacy_count=count, now=1000 + count * 10 + 100)
    if result != 0:
        raise AssertionError(f"legacy Err slot: failed at harness line {result}")
    print(f"PASS legacy Err slot: v2 log with a half-written slot recovers and keeps logging "
          f"({device.get('final_records')} slots)")


def main():
    step = 7 if "--quick" in sys.argv else 1
    image, addresses = build()
    device = Device(image, addresses)

    legacy_err_slot(device)
    stuck_slot(device)
    crash_sweep(device, "fresh flash", b"\xff" * NOR_SIZE, do_format=0, step=step)

    # Old log in every sector, then CLEAR and a new log over the stale records.
    device.erase_nor()
    device.reset()
    result = device.run(PHASE_WORKLOAD, crash_at_op=0, stuck_address=0, samples=1200,
                        do_format=0, now=10_000, acked_records=0, acked_samples=0,
                        config_stage=0, format_done=0)
    if result != 0:
        raise AssertionError(f"stale setup failed at harness line {result}")
    crash_sweep(device, "clear over stale log", device.nor(), do_format=1, step=step)


if __name__ == "__main__":
    main()
