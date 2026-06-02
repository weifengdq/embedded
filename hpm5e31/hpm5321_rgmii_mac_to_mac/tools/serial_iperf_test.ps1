param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('tcp', 'udp')]
    [string]$Protocol,

    [ValidateSet('A', 'B')]
    [string]$ClientBoard,

    [string]$JLinkExe = 'C:/Program Files/SEGGER/JLink_V844/JLink.exe',
    [string]$BoardASerial = '24060502',
    [string]$BoardBSerial = '1120000012',
    [string]$BoardAPort = 'COM13',
    [string]$BoardBPort = 'COM61',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Reset-Board {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SerialNumber,

        [Parameter(Mandatory = $true)]
        [string]$JLinkPath
    )

    $commandFile = Join-Path $env:TEMP ("jlink_reset_{0}.jlink" -f $SerialNumber)
    @(
        'JTAGConf -1,-1'
        'r'
        'g'
        'qc'
    ) | Set-Content -Path $commandFile -Encoding ascii

    & $JLinkPath -NoGui 1 -AutoConnect 1 -SelectEmuBySN $SerialNumber -Device HPM5E31xGNx -If JTAG -Speed 4000 -CommandFile $commandFile | Out-Null
}

function Open-Port {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName
    )

    $port = [System.IO.Ports.SerialPort]::new($PortName, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $port.ReadTimeout = 50
    $port.WriteTimeout = 1000
    $port.Open()
    return $port
}

function Wait-ForPrompt {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$Port,

        [Parameter(Mandatory = $true)]
        [int]$TimeoutMs,

        [ref]$Log
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
        $Log.Value += $Port.ReadExisting()
        if ($Log.Value.Contains('Please enter one of modes')) {
            return $true
        }
    }

    return $false
}

function Busy-Drain {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort[]]$Ports,

        [Parameter(Mandatory = $true)]
        [int]$DurationMs,

        [string]$ClientPortName,

        [ref]$ClientLog,

        [ref]$ServerLog
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt $DurationMs) {
        foreach ($port in $Ports) {
            $chunk = $port.ReadExisting()
            if ($port.PortName -eq $ClientPortName) {
                $ClientLog.Value += $chunk
            } else {
                $ServerLog.Value += $chunk
            }
        }
    }
}

$reportLines = @()
$portA = $null
$portB = $null

if (-not $ClientBoard) {
    if ($Protocol -eq 'tcp') {
        $ClientBoard = 'B'
    } else {
        $ClientBoard = 'A'
    }
}

try {
    $portA = Open-Port -PortName $BoardAPort
    $portB = Open-Port -PortName $BoardBPort

    $portA.DiscardInBuffer()
    $portB.DiscardInBuffer()

    Reset-Board -SerialNumber $BoardASerial -JLinkPath $JLinkExe
    Reset-Board -SerialNumber $BoardBSerial -JLinkPath $JLinkExe

    $startupA = ''
    $startupB = ''
    $readyA = Wait-ForPrompt -Port $portA -TimeoutMs 12000 -Log ([ref]$startupA)
    $readyB = Wait-ForPrompt -Port $portB -TimeoutMs 12000 -Log ([ref]$startupB)

    if (-not $readyA -or -not $readyB) {
        throw "Startup prompt missing. AReady=$readyA, BReady=$readyB"
    }

    $portA.DiscardInBuffer()
    $portB.DiscardInBuffer()

    if ($Protocol -eq 'tcp') {
        $serverChoice = '1'
        $clientChoice = '2'
    } else {
        $serverChoice = '3'
        $clientChoice = '4'
    }

    if ($ClientBoard -eq 'A') {
        $clientPort = $portA
        $serverPort = $portB
        $direction = 'A(COM13) -> B(COM61)'
    } else {
        $clientPort = $portB
        $serverPort = $portA
        $direction = 'B(COM61) -> A(COM13)'
    }

    $serverPort.Write("$serverChoice`r")
    $clientLog = ''
    $serverLog = ''
    Busy-Drain -Ports @($portA, $portB) -DurationMs 1500 -ClientPortName $clientPort.PortName -ClientLog ([ref]$clientLog) -ServerLog ([ref]$serverLog)

    $clientPort.Write("$clientChoice`r")

    $remotePromptHandled = $false
    $reportDetected = $false
    $reportDelay = $null
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    while ($stopwatch.ElapsedMilliseconds -lt 40000) {
        if ($clientPort -eq $portA) {
            $clientLog += $portA.ReadExisting()
            $serverLog += $portB.ReadExisting()
        } else {
            $clientLog += $portB.ReadExisting()
            $serverLog += $portA.ReadExisting()
        }

        if ((-not $remotePromptHandled) -and $clientLog.Contains('Please enter remote server IP')) {
            $clientPort.Write("`r")
            $remotePromptHandled = $true
        }

        if ($remotePromptHandled -and (-not $reportDetected) -and $clientLog.Contains('kbits_per_s=')) {
            $reportDetected = $true
            $reportDelay = [System.Diagnostics.Stopwatch]::StartNew()
        }

        if ($reportDetected -and $reportDelay.ElapsedMilliseconds -ge 500) {
            break
        }
    }

    $report = [regex]::Match($clientLog, 'iperf report:[\s\S]*?kbits_per_s=.*').Value
    $reportLines += "Protocol: $Protocol"
    $reportLines += "Direction: $direction"
    $reportLines += '--- Startup Log A ---'
    $reportLines += $startupA
    $reportLines += '--- Startup Log B ---'
    $reportLines += $startupB
    if ([string]::IsNullOrWhiteSpace($report)) {
        $reportLines += 'Status: report not found'
        $reportLines += '--- Client Log ---'
        $reportLines += $clientLog
        $reportLines += '--- Server Log ---'
        $reportLines += $serverLog
        throw 'iperf report not found'
    }

    $reportLines += $report
    $reportLines += '--- Client Log ---'
    $reportLines += $clientLog
    $reportLines += '--- Server Log ---'
    $reportLines += $serverLog
}
finally {
    if ($OutputPath) {
        $reportLines | Set-Content -Path $OutputPath -Encoding ascii
    }

    if ($null -ne $portA -and $portA.IsOpen) {
        $portA.Close()
    }

    if ($null -ne $portB -and $portB.IsOpen) {
        $portB.Close()
    }
}
