# Read VLAN & Mirror registers via shell spiread commands
$s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
$s.ReadTimeout = 3000
$s.Open()
Start-Sleep 1
# Drain boot
while($s.BytesToRead -gt 0){ $s.ReadExisting()|Out-Null }
Start-Sleep -Milliseconds 500

function SendCmd($cmd, $waitMs=1500) {
    while($s.BytesToRead -gt 0){ $s.ReadExisting()|Out-Null }
    $s.WriteLine($cmd)
    Start-Sleep -Milliseconds $waitMs
    $result = ''
    $deadline = (Get-Date).AddMilliseconds($waitMs + 2000)
    while((Get-Date) -lt $deadline) {
        while($s.BytesToRead -gt 0){ $result += $s.ReadExisting() }
        Start-Sleep -Milliseconds 50
    }
    return $result
}

Write-Host "=== 1. Read LUE_CTRL_0 (0x0310) - VLAN enable bit ==="
SendCmd "spiread 0310" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }

Write-Host "`n=== 2. Read Port4 Default VLAN TAG0 (reg base depends on port) ==="
# Port4 = index 3, PORT_CTRL_ADDR(3, 0x0000) = 0x0000 | (4 << 12) = 0x4000
SendCmd "spiread 4000" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }
SendCmd "spiread 4001" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }

Write-Host "`n=== 3. Read Port2 Default VLAN TAG0 ==="
# Port2 = index 1, PORT_CTRL_ADDR(1, 0x0000) = 0x0000 | (2 << 12) = 0x2000
SendCmd "spiread 2000" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }
SendCmd "spiread 2001" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }

Write-Host "`n=== 4. Read Port4 VLAN Membership ==="
# Port4 = index 3, REG_PORT_VLAN_MEMBERSHIP__4(0x0A04) = 0x0A04 | (4<<12) = 0x4A04
SendCmd "spiread 4A04" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }

Write-Host "`n=== 5. Read PTP control register (0x0200) ==="
SendCmd "spiread 0200" 1 | ForEach-Object { if ($_ -match '0x[0-9A-F]+') { Write-Host $_ } }

Write-Host "`n=== 6. Test VLAN: set Port4 VID=200 and read back ==="
SendCmd "vlan set 4 200" 1 | ForEach-Object { if ($_ -match 'Return') { Write-Host $_ } }
SendCmd "spiread 4000" 1 | ForEach-Object { if ($_ -match 'SPI') { Write-Host $_ } }
SendCmd "spiread 4001" 1 | ForEach-Object { if ($_ -match 'SPI') { Write-Host $_ } }

Write-Host "`n=== 7. Enable VLAN and verify LUE_CTRL_0 changed ==="
SendCmd "vlan on" 1 | ForEach-Object { if ($_ -match 'Return') { Write-Host $_ } }
SendCmd "spiread 0310" 1 | ForEach-Object { if ($_ -match 'SPI') { Write-Host $_ } }

Write-Host "`n=== 8. Read Mirror control register (0x0370) ==="
SendCmd "spiread 0370" 1 | ForEach-Object { if ($_ -match 'SPI') { Write-Host $_ } }

Write-Host "`n=== DONE ==="
$s.Close()
