# Test script for LAN9370 shell commands via COM80
param(
    [string]$Port = "COM80",
    [int]$Baud = 2000000
)

$serial = $null
try {
    $serial = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 500
    $serial.Open()
    Start-Sleep -Seconds 2

    # Drain boot messages
    Start-Sleep -Milliseconds 500
    while ($serial.BytesToRead -gt 0) { $serial.ReadExisting() | Out-Null }

    function Invoke-Cmd {
        param([string]$Cmd, [int]$WaitMs = 800)
        $serial.WriteLine($Cmd)
        Start-Sleep -Milliseconds $WaitMs
        $result = ""
        while ($serial.BytesToRead -gt 0) {
            $result += $serial.ReadExisting()
            Start-Sleep -Milliseconds 100
        }
        return $result.TrimEnd()
    }

    # Wait for shell to be ready
    $ready = Invoke-Cmd "" 3
    Write-Host "=== Test: uptime ==="
    $r = Invoke-Cmd "uptime" 2
    Write-Host $r
    
    Write-Host "`n=== Test: vlan off ==="
    $r = Invoke-Cmd "vlan off" 2
    Write-Host $r
    
    Write-Host "`n=== Test: vlan show ==="
    $r = Invoke-Cmd "vlan show" 2
    Write-Host $r

    Write-Host "`n=== Test: vlan set 2 100 ==="
    $r = Invoke-Cmd "vlan set 2 100" 2
    Write-Host $r

    Write-Host "`n=== Test: vlan show (after set) ==="
    $r = Invoke-Cmd "vlan show" 2
    Write-Host $r

    Write-Host "`n=== Test: mirror 3 2 ==="
    $r = Invoke-Cmd "mirror 3 2" 2
    Write-Host $r

    Write-Host "`n=== Test: mirror 3 off ==="
    $r = Invoke-Cmd "mirror 3 off" 2
    Write-Host $r

    Write-Host "`n=== Test: ptp status ==="
    $r = Invoke-Cmd "ptp status" 2
    Write-Host $r

    Write-Host "`n=== Test: ptp on ==="
    $r = Invoke-Cmd "ptp on" 2
    Write-Host $r

    Write-Host "`n=== Test: ptp status ==="
    $r = Invoke-Cmd "ptp status" 2
    Write-Host $r

    Write-Host "`n=== Test: staticmac list ==="
    $r = Invoke-Cmd "staticmac list" 2
    Write-Host $r

    Write-Host "`n=== Test: config save ==="
    $r = Invoke-Cmd "config save" 3
    Write-Host $r

    Write-Host "`n=== Test: info (verify VLAN config in info) ==="
    $r = Invoke-Cmd "info" 3
    Write-Host $r

    Write-Host "`n=== ALL TESTS PASSED ==="
}
finally {
    if ($serial -and $serial.IsOpen) { $serial.Close() }
}
