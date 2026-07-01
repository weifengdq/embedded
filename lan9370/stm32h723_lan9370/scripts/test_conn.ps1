$s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
$s.ReadTimeout = 2000
$s.Open()
Start-Sleep 1
# Drain
while($s.BytesToRead -gt 0){ $s.ReadExisting() | Out-Null }
Write-Host "--- Testing connectivity ---"
$s.WriteLine('info')
Start-Sleep 3
$out = ''
$deadline = (Get-Date).AddSeconds(5)
while((Get-Date) -lt $deadline) {
    while($s.BytesToRead -gt 0){ $out += $s.ReadExisting() }
    Start-Sleep -Milliseconds 100
}
Write-Host $out
Write-Host "--- Testing uptime ---"
$s.WriteLine('uptime')
Start-Sleep 2
$out = ''
$deadline = (Get-Date).AddSeconds(3)
while((Get-Date) -lt $deadline) {
    while($s.BytesToRead -gt 0){ $out += $s.ReadExisting() }
    Start-Sleep -Milliseconds 100
}
Write-Host $out
$s.Close()
Write-Host "--- DONE ---"
