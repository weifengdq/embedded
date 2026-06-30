$ErrorActionPreference = "Continue"
$s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
$s.ReadTimeout = 5000
$s.Open()
Start-Sleep 3
while($s.BytesToRead -gt 0){$s.ReadExisting()|Out-Null}

function sc($cmd,$ms=2500) {
    $s.WriteLine($cmd)
    Start-Sleep -Milliseconds $ms
    $o=''
    $dl=(Get-Date).AddMilliseconds($ms+2000)
    while((Get-Date) -lt $dl) {
        while($s.BytesToRead -gt 0){$o+=$s.ReadExisting()}
        Start-Sleep -Milliseconds 30
    }
    return $o
}

Write-Host "=== 1. Baseline ping ==="
ping 192.168.0.68 -n 1 | Select-String TTL
ping 192.168.0.108 -n 1 | Select-String TTL

Write-Host "=== 2. Set PVIDs Port2/4/5 = 100 ==="
sc "vlan set 2 100" 1|Out-Null
sc "vlan set 4 100" 1|Out-Null
sc "vlan set 5 100" 1|Out-Null

Write-Host "=== 3. Write VLAN table VID=100 members=Port2+4+5 ==="
$r = sc "vlanadd 100 0x1A" 2
Write-Host $r.Trim()

Write-Host "=== 4. Enable VLAN ==="
$r = sc "vlan on" 2
Write-Host $r.Trim()

Write-Host "=== 5. VLAN show ==="
$r = sc "vlan show" 2
Write-Host $r.Trim()

Write-Host "=== 6. Ping after VLAN ON (Port2+4+5 in same VID) ==="
ping 192.168.0.68 -n 2 | Select-String TTL
ping 192.168.0.108 -n 2 | Select-String TTL

Write-Host "=== 7. Cleanup vlan off ==="
sc "vlan off" 1|Out-Null
$s.Close()
Write-Host "=== DONE ==="
