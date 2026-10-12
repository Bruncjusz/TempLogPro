"""Time zone vectors: compile the production Src/templogpro_tz.c for Cortex-M3 and
check UTC offsets around real daylight-saving transitions.

The expected instants come from the IANA rules (EU, USA, Australia) and are
written out here, so the test needs no tz database on the PC. Requires ARM GCC
(CubeIDE or ARM_GNU_BIN) and unicorn (pip install -r tests/requirements.txt).
"""
import calendar
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/test_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR

# name: (std offset, dst shift, start rule, end rule, [(UTC transition, offset after)])
ZONES = {
    "Europe/Warsaw": (60, 60, (3, 5, 0, 120), (10, 5, 0, 180), [
        ("2026-03-29 01:00", 120), ("2026-10-25 01:00", 60),
        ("2027-03-28 01:00", 120), ("2027-10-31 01:00", 60),
        ("2028-03-26 01:00", 120), ("2028-10-29 01:00", 60)]),
    "America/New_York": (-300, 60, (3, 2, 0, 120), (11, 1, 0, 120), [
        ("2026-03-08 07:00", -240), ("2026-11-01 06:00", -300),
        ("2027-03-14 07:00", -240), ("2027-11-07 06:00", -300)]),
    "Australia/Sydney": (600, 60, (10, 1, 0, 120), (4, 1, 0, 180), [
        ("2026-04-04 16:00", 600), ("2026-10-03 16:00", 660),
        ("2027-04-03 16:00", 600), ("2027-10-02 16:00", 660)]),
    "UTC (no DST)": (0, 0, (0, 0, 0, 0), (0, 0, 0, 0), []),
    "Asia/Kolkata (no DST)": (330, 0, (0, 0, 0, 0), (0, 0, 0, 0), []),
}
MAX_CASES = 64


def utc(text):
    return calendar.timegm(__import__("time").strptime(text, "%Y-%m-%d %H:%M"))


def cases_for(std, dst, transitions):
    cases = []
    for when, after in transitions:
        before = std if after != std else std + dst
        cases += [(utc(when) - 1, before), (utc(when), after)]
    for probe in ("2026-01-15 12:00", "2026-07-15 12:00", "2029-12-31 23:59", "2030-06-30 00:00"):
        if not transitions:
            cases.append((utc(probe), std))
    return cases


HARNESS = r"""
#include "templogpro_tz.h"
volatile TempLogProConfig zone;
volatile uint32_t case_count;
volatile uint32_t case_utc[%d];
volatile int32_t case_out[%d];
void run_tests(void)
{
    uint32_t i;
    for (i = 0u; i < case_count; i++) {
        case_out[i] = templogpro_tz_offset_minutes((const TempLogProConfig *)&zone, case_utc[i]);
    }
}
""" % (MAX_CASES, MAX_CASES)


def main():
    gcc = shutil.which("arm-none-eabi-gcc")
    if os.environ.get("ARM_GNU_BIN"):
        gcc = str(Path(os.environ["ARM_GNU_BIN"]) / "arm-none-eabi-gcc.exe")
    if not gcc:
        gcc = str(sorted(Path("C:/ST").glob("STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe"))[-1])
    bin_dir = Path(gcc).parent
    suffix = ".exe" if os.name == "nt" else ""
    out = ROOT / "tmp/tz_tests"
    out.mkdir(parents=True, exist_ok=True)
    (out / "harness.c").write_text(HARNESS, encoding="ascii")
    linker = out / "test.ld"
    linker.write_text("""MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K }
SECTIONS { .text : { *(.text*) *(.rodata*) } > FLASH
 .data : { *(.data*) } > RAM AT> FLASH
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) } }
""", encoding="ascii")
    elf, binary = out / "tz.elf", out / "tz.bin"
    subprocess.run([gcc, "-mcpu=cortex-m3", "-mthumb", "-std=gnu11", "-Os", "-Wall", "-Wextra", "-Werror",
                    "-ffreestanding", "-nostdlib", "-fno-builtin", f"-I{ROOT / 'Inc'}",
                    str(out / "harness.c"), str(ROOT / "Src/templogpro_tz.c"), f"-T{linker}",
                    "-Wl,-e,run_tests", "-o", str(elf)], check=True)
    subprocess.run([str(bin_dir / ("arm-none-eabi-objcopy" + suffix)), "-O", "binary", str(elf), str(binary)], check=True)
    symbols = subprocess.check_output([str(bin_dir / ("arm-none-eabi-nm" + suffix)), "-n", str(elf)], text=True)
    addr = {p[2]: int(p[0], 16) for line in symbols.splitlines() if len(p := line.split()) == 3}
    # TempLogProConfig layout: 10 x u32, std, dst, two 6-byte rules (month, week, wday, pad, u16 minute), name
    zone_off = 40
    checks = 0
    for name, (std, dst, start, end, transitions) in ZONES.items():
        cases = cases_for(std, dst, transitions)
        assert len(cases) <= MAX_CASES
        m = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        m.mem_map(0x08000000, 0x10000)
        m.mem_map(0x20000000, 0x8000)
        m.mem_write(0x08000000, binary.read_bytes())
        z = addr["zone"]
        m.mem_write(z + zone_off, struct.pack("<ii", std, dst))
        m.mem_write(z + zone_off + 8, struct.pack("<BBBxH", *start))
        m.mem_write(z + zone_off + 14, struct.pack("<BBBxH", *end))
        m.mem_write(addr["case_count"], struct.pack("<I", len(cases)))
        m.mem_write(addr["case_utc"], struct.pack(f"<{len(cases)}I", *[c[0] for c in cases]))
        m.reg_write(UC_ARM_REG_SP, 0x20008000 - 8)
        m.reg_write(UC_ARM_REG_LR, 0x0800FFF1)
        m.reg_write(UC_ARM_REG_XPSR, 0x01000000)
        m.emu_start(addr["run_tests"] | 1, 0x0800FFF0, count=5_000_000)
        assert m.reg_read(UC_ARM_REG_PC) == 0x0800FFF0, f"{name}: execution limit"
        got = struct.unpack(f"<{len(cases)}i", m.mem_read(addr["case_out"], 4 * len(cases)))
        for (ts, expected), value in zip(cases, got):
            assert value == expected, f"{name}: UTC {ts} -> {value}, expected {expected}"
            checks += 1
    print(f"PASS time zones: {checks} offsets around DST transitions (EU, USA, Australia, fixed)")


if __name__ == "__main__":
    main()
