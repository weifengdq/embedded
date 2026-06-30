param([int]$Seconds = 8)

$programmer = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$portName = "COM80"
$baudRate = 2000000

Add-Type -AssemblyName System.IO.Ports

$port = New-Object System.IO.Ports.SerialPort($portName, $baudRate, "None", 8, "One")
$port.Open()
Start-Sleep -Milliseconds 200

# Reset MCU
& $programmer -c port=SWD -rst 2>&1 | Out-Null
Write-Host "[LOG] MCU reset, collecting $Seconds s..."

$log = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    if ($port.BytesToRead -gt 0) {
        $log += $port.ReadExisting()
    }
    Start-Sleep -Milliseconds 50
}
$port.Close()

Write-Host "=== Serial Log ===" 
Write-Host $log
Write-Host "==================" 
