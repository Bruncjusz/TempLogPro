# Flashes Release/TempLogPro.bin at 0x08000000 over ST-Link SWD, verifies it
# and resets the MCU. Connects under reset, which this board needs. Only the
# pages covered by the image are erased: no mass erase, no option bytes, and
# the external Winbond memory is never touched.
param(
    [string]$Image = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Release\TempLogPro.bin'),
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'
$plugins = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins'
$cli = (Get-ChildItem $plugins -Directory -Filter 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*' |
        Sort-Object Name -Descending | Select-Object -First 1).FullName + '\tools\bin\STM32_Programmer_CLI.exe'
if (-not (Test-Path $cli)) { throw "STM32_Programmer_CLI not found under $plugins" }

if ($ListOnly) {
    & $cli -l stlink
    exit $LASTEXITCODE
}
if (-not (Test-Path $Image)) { throw "Image not found: $Image" }

'{0}  {1}' -f (Get-FileHash $Image -Algorithm SHA256).Hash.ToLower(), $Image
& $cli -c port=SWD mode=UR reset=HWrst -w $Image 0x08000000 -v -rst
if ($LASTEXITCODE -ne 0) { throw "STM32_Programmer_CLI failed with exit code $LASTEXITCODE" }
