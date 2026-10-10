param([string]$CubeIdeRoot = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE", [switch]$NoLcd, [switch]$Bare, [switch]$Stop, [switch]$StopDevices, [switch]$StopDevicesRescue)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$variantDirectory = if ($StopDevicesRescue) { 'build_stop_devices_rescue' } elseif ($StopDevices) { 'build_stop_devices' } elseif ($Stop) { 'build_stop' } elseif ($Bare) { 'build_bare' } elseif ($NoLcd) { 'build_no_lcd' } else { 'build' }
$out = Join-Path $root ('diagnostics\module_probe\' + $variantDirectory)
$gcc = Get-ChildItem "$CubeIdeRoot\plugins" -Recurse -Filter arm-none-eabi-gcc.exe |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $gcc) { throw 'ARM compiler not found.' }
$bin = Split-Path $gcc
New-Item -ItemType Directory -Force $out | Out-Null
$flags = @('-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-std=gnu11','-Os',
    '-DSTM32L1','-DSTM32L152RCTx','-DSTM32L152xC','-DSTM32',
    '-ffunction-sections','-fdata-sections','-Wall','--specs=nano.specs')
if ($NoLcd) { $flags += '-DPROBE_INIT_LCD=0' }
if ($Bare -or $Stop -or $StopDevices -or $StopDevicesRescue) { $flags += '-DPROBE_INIT_LCD=0' }
if ($Bare) { $flags += '-DPROBE_BARE=1' }
if ($Stop) { $flags += '-DPROBE_BARE_STOP=1' }
if ($StopDevices) { $flags += '-DPROBE_BARE_STOP_DEVICES=1' }
if ($StopDevicesRescue) { $flags += '-DPROBE_BARE_STOP_RESCUE=1' }
foreach ($inc in @('Inc','Drivers/STM32L1xx_HAL_Driver/Inc',
    'Drivers/STM32L1xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L1xx/Include',
    'Drivers/CMSIS/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc',
    'Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc',
    'Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc')) {
    $flags += "-I$(Join-Path $root $inc)"
}
# No application, logger, EEPROM/config, RTC-init or report/filesystem object.
$sources = @('diagnostics/module_probe/module_probe.c','diagnostics/module_probe/protocol.c',
    'diagnostics/module_probe/sensor_test.c',
    'diagnostics/module_probe/usb_probe.c','diagnostics/module_probe/storage_absent.c',
    'Src/syscalls.c','Src/sysmem.c','Src/templogpro_diag.c','Src/templogpro_power.c',
    'Src/st_usb/templogpro_usb_st_hid.c','Src/st_usb/templogpro_usb_st_it.c','Src/st_usb/usbd_conf.c',
    'Src/st_usb/msc/usbd_msc_bot.c','Src/st_usb/msc/usbd_msc_data.c','Src/st_usb/msc/usbd_msc_scsi.c',
    'Startup/startup_stm32l152rctx.s')
$sources += Get-ChildItem "$root\Src\st_usb\hal" -Filter *.c | ForEach-Object FullName
$sources += Get-ChildItem "$root\Src\st_usb\usbd" -Filter *.c | ForEach-Object FullName
$objects = @()
$inputs = @()
foreach ($source in $sources) {
    $path = if ([IO.Path]::IsPathRooted($source)) { $source } else { Join-Path $root $source }
    $obj = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    & $gcc @flags -c $path -o $obj
    if ($LASTEXITCODE) { throw "Compile failed: $source" }
    $objects += $obj
    $inputs += Get-FileHash $path -Algorithm SHA256
}
$elf = Join-Path $out 'TempLogPro_module_probe.elf'
& $gcc @objects -o $elf -mcpu=cortex-m3 -mthumb -mfloat-abi=soft `
    "-T$root\STM32L152RCTX_FLASH.ld" --specs=nosys.specs --specs=nano.specs `
    '-Wl,--gc-sections' "-Wl,-Map=$out\TempLogPro_module_probe.map" '-Wl,--start-group' -lc -lm '-Wl,--end-group'
if ($LASTEXITCODE) { throw 'Link failed.' }
$symbols = & "$bin\arm-none-eabi-nm.exe" --defined-only $elf
if ($LASTEXITCODE) { throw 'Symbol inspection failed.' }
$forbidden = 'templogpro_(app_|logger_|config_|rtc_|usb_files_|hw_flash_write|hw_flash_erase)|w25x40_(write|page_program|sector_erase)|cdc_process_(char|line)|templogpro_normal_|lcd_prepare_clock'
if ($symbols -match $forbidden) { throw "Unexpected write/application symbol linked: $($symbols -match $forbidden)" }
$symbols | Set-Content "$out\symbols.txt" -Encoding ascii
foreach ($format in @(@('ihex','hex'), @('binary','bin'))) {
    & "$bin\arm-none-eabi-objcopy.exe" -O $format[0] $elf "$out\TempLogPro_module_probe.$($format[1])"
    if ($LASTEXITCODE) { throw 'Image generation failed.' }
}
# Included board/USB source files must be tracked as build inputs as well.
$inputs += Get-FileHash "$root\Src\board.c", "$root\Src\board.h", "$root\Src\board_regs.h", "$root\Src\drv_lcd.c", "$root\Src\drv_sensor_i2c.c", "$root\Src\drv_w25x40.c", "$root\Src\drv_adc.c", "$root\Src\st_usb\msc\usbd_msc.c" -Algorithm SHA256
$inputs += Get-FileHash "$root\diagnostics\module_probe\sensor_bus.inc", "$root\diagnostics\module_probe\sensor_test.h", "$root\diagnostics\module_probe\probe.h" -Algorithm SHA256
$inputs | Select-Object Path, Hash | ConvertTo-Json | Set-Content "$out\source-hashes.json" -Encoding ascii
& "$bin\arm-none-eabi-size.exe" $elf
Get-FileHash "$out\TempLogPro_module_probe.bin"
Write-Host 'Diagnostic build passed: no logger/config/filesystem or flash-write entry points.'
