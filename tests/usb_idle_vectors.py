"""Compile production idle decisions for ARM and run them with mocked IRQs.

Requires ARM GCC (CubeIDE or ARM_GNU_BIN) and the PC-only unicorn package.
This checks C logic, not physical USB, power consumption or silicon wakeup.
"""
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tmp/test_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_XPSR


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|uint32_t) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    if match is None:
        raise RuntimeError(f"Definition not found: {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    gcc = shutil.which("arm-none-eabi-gcc")
    if os.environ.get("ARM_GNU_BIN"):
        gcc = str(Path(os.environ["ARM_GNU_BIN"]) / "arm-none-eabi-gcc.exe")
    if not gcc:
        candidates = sorted(Path("C:/ST").glob("STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe"))
        if not candidates:
            raise RuntimeError("Set ARM_GNU_BIN to your ARM GCC bin directory")
        gcc = str(candidates[-1])
    bin_dir = Path(gcc).parent
    suffix = ".exe" if os.name == "nt" else ""
    out = ROOT / "tmp/usb_idle_tests"
    out.mkdir(parents=True, exist_ok=True)
    sources = {
        "Src/templogpro_app.c": ["templogpro_app_should_idle_sleep", "templogpro_app_should_stop_sleep"],
        "Src/st_usb/msc/usbd_msc.c": ["USBD_MSC_CDC_WorkPending"],
        "Src/st_usb/templogpro_usb_st_hid.c": ["templogpro_usb_st_work_pending"],
        "Src/templogpro_power.c": ["power_add_u32", "templogpro_power_enter_idle"],
    }
    definitions = []
    for path, names in sources.items():
        source = (ROOT / path).read_text(encoding="utf-8-sig")
        definitions.extend(function(source, name) for name in names)
    (out / "usb_idle_functions.inc").write_text("\n\n".join(definitions), encoding="ascii")
    linker = out / "test.ld"
    linker.write_text("""MEMORY {
 FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .data : { *(.data*) } > RAM AT> FLASH
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
""", encoding="ascii")
    includes = ["Inc", "Drivers/CMSIS/Include", "Drivers/CMSIS/Device/ST/STM32L1xx/Include",
                "Drivers/STM32L1xx_HAL_Driver/Inc", "Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
                "Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc"]
    for opt in ("-O0", "-Os"):
        for enabled in (0, 1):
            name = f"idle_{opt[1:]}_{enabled}"
            elf, binary = out / (name + ".elf"), out / (name + ".bin")
            command = [gcc, "-mcpu=cortex-m3", "-mthumb", "-std=gnu11", opt,
                       "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-nostdlib",
                       "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fno-builtin",
                       "-DSTM32L152xC", f"-DTEMPLOGPRO_IDLE_WFI_ENABLE={enabled}",
                       *[f"-I{ROOT / inc}" for inc in includes], f"-I{out}",
                       str(ROOT / "tests/usb_idle_harness.c"), f"-T{linker}",
                       "-Wl,-e,run_tests", "-o", str(elf)]
            subprocess.run(command, check=True)
            subprocess.run([str(bin_dir / ("arm-none-eabi-objcopy" + suffix)), "-O", "binary", str(elf), str(binary)], check=True)
            symbols = subprocess.check_output([str(bin_dir / ("arm-none-eabi-nm" + suffix)), "-n", str(elf)], text=True)
            addresses = {parts[2]: int(parts[0], 16) for line in symbols.splitlines() if len(parts := line.split()) == 3}
            machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
            machine.mem_map(0x08000000, 0x10000)
            machine.mem_map(0x20000000, 0x8000)
            machine.mem_write(0x08000000, binary.read_bytes())
            machine.reg_write(UC_ARM_REG_SP, 0x20008000 - 8)
            machine.reg_write(UC_ARM_REG_LR, 0x0800FFF1)
            machine.reg_write(UC_ARM_REG_XPSR, 0x01000000)
            machine.emu_start(addresses["run_tests"] | 1, 0x0800FFF0, count=200000)
            if machine.reg_read(UC_ARM_REG_PC) != 0x0800FFF0:
                raise AssertionError(f"{name}: execution limit reached")
            result = machine.reg_read(UC_ARM_REG_R0)
            count = struct.unpack("<I", machine.mem_read(addresses["test_count"], 4))[0]
            if result:
                raise AssertionError(f"{name}: check failed at harness line {result}")
            print(f"PASS {name}: {count} compiled-C checks")


if __name__ == "__main__":
    main()
