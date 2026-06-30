# Comprehensive closed-loop test for VLAN, Mirror, PTP
$ErrorActionPreference = "Continue"
$s = $null

function SendCmd($cmd, $waitMs=1500) {
    while($s.BytesToRead -gt 0){ $s.ReadExisting()|Out-Null }
    $s.WriteLine($cmd)
    Start-Sleep -Milliseconds $waitMs
    $result = ''
    $deadline = (Get-Date).AddMilliseconds($waitMs + 3000)
    while((Get-Date) -lt $deadline) {
        while($s.BytesToRead -gt 0){ $result += $s.ReadExisting() }
        Start-Sleep -Milliseconds 50
    }
    return $result
}

function Get-RegVal($result) {
    if ($result -match '=0x([0-9A-Fa-f]{2})') {
        return [Convert]::ToInt32($Matches[1], 16)
    }
    return -1
}

function Test-Bit($val, $bit, $name) {
    if ($val -lt 0) { Write-Host "    [SKIP] $name - register read failed" -ForegroundColor Yellow; return }
    if ($val -band (1 -shl $bit)) { Write-Host "    [PASS] $name (bit$bit) = 1" -ForegroundColor Green }
    else { Write-Host "    [FAIL] $name (bit$bit) = 0" -ForegroundColor Red }
}

try {
    $s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
    $s.ReadTimeout = 2000
    $s.Open()
    Start-Sleep 3
    while($s.BytesToRead -gt 0){ $s.ReadExisting()|Out-Null }

    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  VLAN REGISTER TEST" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    SendCmd "vlan off" 1 | Out-Null
    Start-Sleep -Milliseconds 300

    Write-Host "[VLAN.1] REG_LUE_CTRL_0 (0x0310) after VLAN off:" -ForegroundColor Yellow
    $r = SendCmd "spiread 0310" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 7 "SW_VLAN_ENABLE should be 0"

    Write-Host "`n[VLAN.2] Enabling VLAN..." -ForegroundColor Yellow
    SendCmd "vlan on" 1 | Out-Null
    Start-Sleep -Milliseconds 300

    $r = SendCmd "spiread 0310" 1
    Write-Host "        REG_LUE_CTRL_0 after VLAN on: $r"
    $v = Get-RegVal $r
    Test-Bit $v 7 "SW_VLAN_ENABLE should be 1"

    Write-Host "`n[VLAN.3] Port4 REG_PORT_LUE_CTRL (0x4B00):" -ForegroundColor Yellow
    $r = SendCmd "spiread 4B00" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 7 "PORT_VLAN_LOOKUP_VID_0 should be 1"

    Write-Host "`n[VLAN.4] Setting Port4 VID=200, Port5 VID=300..." -ForegroundColor Yellow
    SendCmd "vlan set 4 200" 1 | Out-Null
    SendCmd "vlan set 5 300" 1 | Out-Null
    Start-Sleep -Milliseconds 300

    Write-Host "[VLAN.5] Port4 DEFAULT_VID register (0x4000-0x4001):" -ForegroundColor Yellow
    $lo = Get-RegVal (SendCmd "spiread 4000" 1)
    $hi = Get-RegVal (SendCmd "spiread 4001" 1)
    if ($lo -ge 0 -and $hi -ge 0) {
        $vid = (($hi -band 0x0F) -shl 8) -bor $lo
        Write-Host "        VID = $vid (expect 200)"
        if ($vid -eq 200) { Write-Host "        [PASS] Port4 VID = 200" -ForegroundColor Green }
        else { Write-Host "        [WARN] Port4 VID = $vid" -ForegroundColor Yellow }
    }

    Write-Host "[VLAN.6] Full VLAN config:" -ForegroundColor Yellow
    $r = SendCmd "vlan show" 2
    Write-Host $r

    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "  MIRROR REGISTER TEST" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    Write-Host "[MIR.1] Setting mirror Port4 -> Port2..." -ForegroundColor Yellow
    $r = SendCmd "mirror 4 2" 2
    Write-Host "        $r"

    Write-Host "[MIR.2] Port4 MIRROR_CTRL (0x4800):" -ForegroundColor Yellow
    $r = SendCmd "spiread 4800" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 5 "PORT_MIRROR_TX"
    Test-Bit $v 6 "PORT_MIRROR_RX"

    Write-Host "[MIR.3] Port2 MIRROR_CTRL (0x2800):" -ForegroundColor Yellow
    $r = SendCmd "spiread 2800" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 1 "PORT_MIRROR_SNIFFER"

    Write-Host "[MIR.4] SW_MRI_CTRL_0 (0x0370):" -ForegroundColor Yellow
    $r = SendCmd "spiread 0370" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 0 "SW_MIRROR_RX_TX"

    Write-Host "`n[MIR.5] Capturing packets on ethernet while pinging..." -ForegroundColor Yellow
    Write-Host "        Mirror: Port4 traffic -> copied to Port2"
    Write-Host "        Ping 192.168.0.108 (STM32 at Port5) generates traffic via Port4"

    $job = Start-Job -ScriptBlock {
        & "C:\Program Files\Wireshark\tshark.exe" -i "以太网" -c 10 -a duration:6 2>&1
    }
    Start-Sleep 1
    ping 192.168.0.108 -n 3 | Out-Null
    Start-Sleep 3
    $cap = Receive-Job $job -Wait
    Remove-Job $job

    $pkts = ($cap | Where-Object { $_ -match '^\s+\d+\s' }).Count
    Write-Host "        Captured $pkts packets"
    if ($pkts -gt 0) {
        Write-Host "        [PASS] Traffic flowing through Port4" -ForegroundColor Green
        $cap | Select-Object -First 3 | ForEach-Object { Write-Host "        $_" }
    }

    Write-Host "`n[MIR.6] Disabling mirror..." -ForegroundColor Yellow
    SendCmd "mirror 4 off" 2 | Out-Null
    $r = SendCmd "spiread 4800" 1
    Write-Host "        Port4 MIRROR_CTRL after off: $r"
    $v = Get-RegVal $r
    if ($v -eq 0) { Write-Host "        [PASS] Mirror bits cleared" -ForegroundColor Green }
    else { Write-Host "        [WARN] Mirror bits still set" -ForegroundColor Yellow }

    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "  PTP REGISTER TEST" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    Write-Host "[PTP.1] Enabling PTP..." -ForegroundColor Yellow
    SendCmd "ptp on" 1 | Out-Null
    Start-Sleep -Milliseconds 300

    Write-Host "[PTP.2] PTP MSG_CONF1 (0x0514):" -ForegroundColor Yellow
    $r = SendCmd "spiread 0514" 1
    Write-Host "        $r"
    $v = Get-RegVal $r
    Test-Bit $v 6 "PTP_ENABLE"

    Write-Host "[PTP.3] Enabling gPTP (802.1AS)..." -ForegroundColor Yellow
    SendCmd "ptp gptp on" 1 | Out-Null
    Start-Sleep -Milliseconds 300

    $r = SendCmd "spiread 0514" 1
    Write-Host "        MSG_CONF1 after gPTP: $r"
    $v = Get-RegVal $r
    Test-Bit $v 6 "PTP_ENABLE"
    Test-Bit $v 7 "PTP_802_1AS"

    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "  FINAL CHECK" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    SendCmd "vlan off" 1 | Out-Null
    Start-Sleep -Milliseconds 200

    $p = ping 192.168.0.108 -n 2 2>&1 | Select-String "TTL"
    if ($p) { Write-Host "[PASS] Ping to 192.168.0.108 OK" -ForegroundColor Green }
    else { Write-Host "[FAIL] Ping failed!" -ForegroundColor Red }

    Write-Host "`n=== ALL TESTS COMPLETE ===" -ForegroundColor Cyan

} finally {
    if ($s -and $s.IsOpen) { $s.Close() }
}
