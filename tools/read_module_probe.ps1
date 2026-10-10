param([string]$Port = 'COM3', [switch]$BackupFlash, [switch]$TestSensor)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root ('diagnostics\module_probe\results_' + (Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory $out | Out-Null
$serial = [IO.Ports.SerialPort]::new($Port, 115200, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
$serial.ReadTimeout = 4000
$serial.WriteTimeout = 4000
$serial.DtrEnable = $false
$serial.NewLine = "`n"

function Invoke-Probe([string]$Command) {
    $serial.WriteLine($Command)
    $lines = [Collections.Generic.List[string]]::new()
    $start = $serial.ReadLine().TrimEnd("`r")
    if ($start -ne 'BEGIN') { throw "Unexpected framing: $start" }
    do {
        $line = $serial.ReadLine().TrimEnd("`r")
        if ($line -eq 'END') { break }
        if ($line.StartsWith('ERROR')) { throw "$Command : $line" }
        $lines.Add($line)
        if ($lines.Count -gt 40) { throw 'Unbounded response.' }
    } while ($true)
    return $lines.ToArray()
}

try {
    # Windows can retain a stale COM handle briefly after a firmware reset.
    # Retry the read-only handshake only, never a partially executed test.
    foreach ($attempt in 1..5) {
        try {
            $serial.Open()
            $serial.DiscardInBuffer()
            $identity = Invoke-Probe 'PING'
            break
        } catch {
            try { $serial.Close() } catch {}
            if ($attempt -eq 5) { throw }
            Start-Sleep -Milliseconds 1000
        }
    }
    if ($identity -ne 'TEMPLOGPRO_MODULE_PROBE_V1 READ_ONLY LOGGER_DISABLED' -and
        $identity -ne 'TEMPLOGPRO_MODULE_PROBE_V2 BOUNDED_SENSOR_TEST LOGGER_DISABLED') {
        throw 'Not the expected diagnostic firmware. No further commands sent.'
    }
    $results = [ordered]@{ port=$Port; host_time=(Get-Date -Format o); identity=$identity }
    foreach ($command in @('GET MCU','GET CLOCKS','GET GPIO A','GET GPIO B','GET GPIO C',
        'GET GPIO D','GET GPIO E','GET RTC','GET SENSOR','GET FLASH','GET VDD')) {
        $result = @(Invoke-Probe $command)
        $results[$command] = $result
        Write-Host "$command`n$($result -join "`n")"
    }
    $results['SENSOR_REPEAT'] = @(Invoke-Probe 'GET SENSOR')
    $results['CLOCKS_AFTER'] = @(Invoke-Probe 'GET CLOCKS')
    if ($TestSensor) {
        if ($identity -notlike 'TEMPLOGPRO_MODULE_PROBE_V2 *') { throw 'Sensor test requires V2.' }
        foreach ($iteration in 1..3) {
            $result = @(Invoke-Probe 'TEST SENSOR')
            $results["TEST_SENSOR_$iteration"] = $result
            $results | ConvertTo-Json -Depth 4 | Set-Content "$out\registers.json" -Encoding utf8
            Write-Host ($result -join "`n")
            if (-not ($result -match '^TEST result=PASS .*restored=1.*thresholds_unchanged=1')) {
                throw 'Sensor test did not pass; result saved. Inspect restoration status before proceeding.'
            }
        }
        $results['SENSOR_AFTER_TESTS'] = @(Invoke-Probe 'GET SENSOR')
    }
    $results | ConvertTo-Json -Depth 4 | Set-Content "$out\registers.json" -Encoding utf8
    if ($BackupFlash) {
        $hashes = @()
        for ($pass = 1; $pass -le 2; $pass++) {
            $bytes = [byte[]]::new(524288)
            for ($offset = 0; $offset -lt $bytes.Length; $offset += 256) {
                $hexAddress = '{0:X6}' -f $offset
                $reply = @(Invoke-Probe "FLASH READ $hexAddress")
                if ($reply.Count -ne 1 -or $reply[0] -notmatch "^FLASH $hexAddress ([0-9A-F]{512})$") {
                    throw "Invalid flash block at $hexAddress"
                }
                $hex = $Matches[1]
                for ($i = 0; $i -lt 256; $i++) {
                    $bytes[$offset + $i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
                }
                if (($offset % 65536) -eq 0) { Write-Host "Flash backup pass $pass : $offset / 524288" }
            }
            $path = "$out\winbond_pass$pass.bin"
            [IO.File]::WriteAllBytes($path, $bytes)
            $hashes += (Get-FileHash $path -Algorithm SHA256).Hash
        }
        if ($hashes[0] -ne $hashes[1]) { throw 'External flash reads differ; do not treat this as a verified backup.' }
        Write-Host "Two identical external flash reads, SHA256=$($hashes[0])"
        $hashes[0] | Set-Content "$out\winbond.sha256.txt" -Encoding ascii
    }
    Write-Host "Results: $out"
} finally {
    try { if ($serial.IsOpen) { $serial.Close() } } catch { Write-Warning $_ }
    try { $serial.Dispose() } catch { Write-Warning $_ }
}
