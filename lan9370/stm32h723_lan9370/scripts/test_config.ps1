$s = New-Object System.IO.Ports.SerialPort('COM80', 2000000, 'None', 8, 'One')
$s.ReadTimeout = 5000
$s.Open()
Start-Sleep 1
while($s.BytesToRead -gt 0){ $s.ReadExisting() | Out-Null }

Write-Host "--- config save (may take ~3s for flash erase) ---"
$s.WriteLine('config save')
Start-Sleep -Milliseconds 500
$out = ''
$deadline = (Get-Date).AddSeconds(10)
while((Get-Date) -lt $deadline) {
    while($s.BytesToRead -gt 0){ 
        $chunk = $s.ReadExisting()
        $out += $chunk
        Write-Host -NoNewline $chunk
    }
    Start-Sleep -Milliseconds 100
}
Write-Host "`n--- config show ---"
$s.WriteLine('config show')
Start-Sleep 2
$out = ''
$deadline = (Get-Date).AddSeconds(3)
while((Get-Date) -lt $deadline) {
    while($s.BytesToRead -gt 0){ $out += $s.ReadExisting() }
    Start-Sleep -Milliseconds 100
}
Write-Host $out
Write-Host "--- done ---"
$s.Close()
