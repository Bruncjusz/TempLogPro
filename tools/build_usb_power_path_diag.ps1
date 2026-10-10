param(
    [string]$CubeIdeRoot = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "Release"
$outputDir = Join-Path $projectRoot "diagnostics\usb_power_path"
$gcc = Get-ChildItem (Join-Path $CubeIdeRoot "plugins") -Recurse -Filter "arm-none-eabi-gcc.exe" |
    Select-Object -First 1 -ExpandProperty FullName
$objcopy = Get-ChildItem (Join-Path $CubeIdeRoot "plugins") -Recurse -Filter "arm-none-eabi-objcopy.exe" |
    Select-Object -First 1 -ExpandProperty FullName
$size = Get-ChildItem (Join-Path $CubeIdeRoot "plugins") -Recurse -Filter "arm-none-eabi-size.exe" |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $gcc -or -not $objcopy -or -not $size) {
    throw "Nie znaleziono narzedzi GNU Arm w instalacji STM32CubeIDE."
}
if (-not (Test-Path (Join-Path $releaseDir "objects.list"))) {
    throw "Najpierw zbuduj konfiguracje Release w STM32CubeIDE."
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$diagObject = Join-Path $outputDir "templogpro_usb_st_hid.diag.o"
$diagElf = Join-Path $outputDir "TempLogPro_usb_power_path_diag.elf"
$diagMap = Join-Path $outputDir "TempLogPro_usb_power_path_diag.map"
$diagHex = Join-Path $outputDir "TempLogPro_usb_power_path_diag.hex"
$diagBin = Join-Path $outputDir "TempLogPro_usb_power_path_diag.bin"
$diagObjects = Join-Path $outputDir "objects_diag.list"

$includes = @(
    "Inc",
    "Drivers\STM32L1xx_HAL_Driver\Inc",
    "Drivers\STM32L1xx_HAL_Driver\Inc\Legacy",
    "Drivers\CMSIS\Device\ST\STM32L1xx\Include",
    "Drivers\CMSIS\Include",
    "Middlewares\ST\STM32_USB_Device_Library\Core\Inc",
    "Middlewares\ST\STM32_USB_Device_Library\Class\HID\Inc",
    "Middlewares\ST\STM32_USB_Device_Library\Class\MSC\Inc"
)
$compileArgs = @(
    (Join-Path $projectRoot "Src\st_usb\templogpro_usb_st_hid.c"),
    "-mcpu=cortex-m3", "-std=gnu11",
    "-DSTM32L1", "-DSTM32L152RCTx", "-DSTM32L152xC", "-DSTM32",
    "-DTEMPLOGPRO_USB_POWER_PATH_DIAG=1U",
    "-c", "-Os", "-ffunction-sections", "-fdata-sections", "-Wall",
    "--specs=nano.specs", "-mfloat-abi=soft", "-mthumb",
    "-o", $diagObject
)
foreach ($include in $includes) {
    $compileArgs += "-I$(Join-Path $projectRoot $include)"
}
& $gcc @compileArgs
if ($LASTEXITCODE -ne 0) {
    throw "Kompilacja obiektu diagnostycznego nie powiodla sie."
}

$normalObject = '"./Src/st_usb/templogpro_usb_st_hid.o"'
$diagObjectEntry = '"' + ($diagObject -replace '\\', '/') + '"'
$objectLines = Get-Content (Join-Path $releaseDir "objects.list") |
    ForEach-Object { if ($_ -eq $normalObject) { $diagObjectEntry } else { $_ } }
$objectLines | Set-Content -Encoding ascii $diagObjects

$linkerScript = Join-Path $projectRoot "STM32L152RCTX_FLASH.ld"
$linkArgs = @(
    "-o", $diagElf,
    "@$diagObjects",
    "-mcpu=cortex-m3", "-T$linkerScript", "--specs=nosys.specs",
    "-Wl,-Map=$diagMap", "-Wl,--gc-sections", "-static", "--specs=nano.specs",
    "-mfloat-abi=soft", "-mthumb", "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group"
)
Push-Location $releaseDir
try {
    & $gcc @linkArgs
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) {
    throw "Linkowanie obrazu diagnostycznego nie powiodlo sie."
}

& $objcopy -O ihex $diagElf $diagHex
if ($LASTEXITCODE -ne 0) { throw "Tworzenie HEX nie powiodlo sie." }
& $objcopy -O binary $diagElf $diagBin
if ($LASTEXITCODE -ne 0) { throw "Tworzenie BIN nie powiodlo sie." }
& $size $diagElf

Write-Host "Gotowy obraz diagnostyczny: $diagHex"
