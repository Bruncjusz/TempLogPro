param([string]$CubeIdeRoot = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$backup = Join-Path $root 'backup\main_before_module_diag_20260919_231527'
$image = Join-Path $backup 'TempLogPro.bin'
$expected = 'DF4C05F0F1832B16CECBC1F1FDEF0A85D08C5DB427856A89AD87B971A0F56784'
if ((Get-FileHash $image -Algorithm SHA256).Hash -ne $expected) { throw 'Main firmware backup checksum mismatch.' }
$cli = Get-ChildItem "$CubeIdeRoot\plugins" -Recurse -Filter STM32_Programmer_CLI.exe |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $cli) { throw 'STM32 programmer not found.' }
# Program only the main image. Do not erase/write EEPROM, option bytes or external SPI flash.
& $cli -c port=SWD mode=UR freq=1000 sn=37FF71064E5734361D081543 -d $image 0x08000000 -v -rst
if ($LASTEXITCODE) { throw 'Restore failed; retain all backups and inspect the programmer output.' }
Write-Host 'Main firmware 0.8.12-power restored and verified. Logger may resume according to stored state.'
