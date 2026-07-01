# Test: Egress Members stability + VLAN VID + Config persist
$s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
$s.ReadTimeout = 2000
$s.Open()
Start-Sleep 3
while($s.BytesToRead -gt 0){$s.ReadExisting()|Out-Null}

function sc($cmd,$w=2){$s.WriteLine($cmd);Start-Sleep $w;$o='';$d=(Get-Date).AddMilliseconds($w+3000);while((Get-Date)-lt $d){while($s.BytesToRead -gt 0){$o+=$s.ReadExisting();Start-Sleep -Milliseconds 30}};return $o}

# 1. Egress Members stability test
Write-Host "=== TEST 1: Egress Members stability (3x info) ===" -ForegroundColor Cyan
sc "vlan off" 1|Out-Null
for ($i=0; $i -lt 3; $i++) {
    $r = sc "info" 3
    # Extract Egress Members lines
    $lines = $r -split "`r`n" | Where-Object { $_ -match '^\s+\d+\s+\|' }
    $blanks = ($lines | Where-Object { $_ -notmatch '\d\s+\|\s*\d' }).Count
    Write-Host "  info #$($i+1): $($lines.Count) port lines, $blanks blank membership(s)"
}
Write-Host ""

# 2. VID write and read-back + connectivity check
Write-Host "=== TEST 2: VID write + ping ===" -ForegroundColor Cyan
Write-Host "  Setting Port2 VID=100, Port4 VID=100 (same VLAN)..."
sc "vlan set 2 100" 1|Out-Null
sc "vlan set 4 100" 1|Out-Null

# Read back VID via spiread
$r = sc "spiread 0x2000" 1  # Port2 VID low byte
Write-Host "  Port2 VID byte0: $r"
$r = sc "spiread 0x2001" 1  # Port2 VID high byte  
Write-Host "  Port2 VID byte1: $r"

# Ping test (VLAN is OFF, so VID change should NOT affect connectivity)
Write-Host "  Ping 192.168.0.108 (should work, VLAN is OFF)..."
$p = ping 192.168.0.108 -n 2 2>&1 | Select-String TTL
if ($p) { Write-Host "  [PASS] Ping OK" -ForegroundColor Green }
else { Write-Host "  [FAIL] Ping failed!" -ForegroundColor Red }

Write-Host "  Ping 192.168.0.68 (Port2 device)..."
$p = ping 192.168.0.68 -n 2 2>&1 | Select-String TTL
if ($p) { Write-Host "  [PASS] Ping to Port2 device OK" -ForegroundColor Green }
else { Write-Host "  [FAIL] Ping to Port2 failed!" -ForegroundColor Red }

# 3. Config persist test
Write-Host "`n=== TEST 3: Config save/load ===" -ForegroundColor Cyan
Write-Host "  Saving config..."
sc "config save" 5|Out-Null

Write-Host "  Loading config..."
$r = sc "config load" 3
Write-Host "  $r"

Write-Host "  Showing config..."
$r = sc "config show" 2
Write-Host $r

Write-Host "`n=== ALL TESTS DONE ===" -ForegroundColor Cyan
$s.Close()
