param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('tcp', 'udp')]
    [string]$Protocol,

    [ValidateSet('A', 'B')]
    [string]$ClientBoard,

    [int]$Rounds = 10,

    [string]$JLinkExe = 'C:/Program Files/SEGGER/JLink_V844/JLink.exe',
    [string]$BoardASerial = '24060502',
    [string]$BoardBSerial = '1120000012',
    [string]$BoardAPort = 'COM13',
    [string]$BoardBPort = 'COM61',
    [string]$OutputDir = (Join-Path $env:TEMP 'serial_iperf_stability')
)

$ErrorActionPreference = 'Stop'

if ($Rounds -lt 1) {
    throw 'Rounds must be greater than zero.'
}

if (-not $ClientBoard) {
    if ($Protocol -eq 'tcp') {
        $ClientBoard = 'B'
    } else {
        $ClientBoard = 'A'
    }
}

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$scriptPath = Join-Path $PSScriptRoot 'serial_iperf_test.ps1'
$summary = New-Object System.Collections.Generic.List[object]
$failedRounds = 0

for ($round = 1; $round -le $Rounds; $round++) {
    $logPath = Join-Path $OutputDir ("{0}_round{1:00}.txt" -f $Protocol, $round)
    $ok = $true
    $errorText = ''

    try {
        & $scriptPath `
            -Protocol $Protocol `
            -ClientBoard $ClientBoard `
            -JLinkExe $JLinkExe `
            -BoardASerial $BoardASerial `
            -BoardBSerial $BoardBSerial `
            -BoardAPort $BoardAPort `
            -BoardBPort $BoardBPort `
            -OutputPath $logPath | Out-Null
    }
    catch {
        $ok = $false
        $errorText = $_.Exception.Message
        $failedRounds++
    }

    $reportText = ''
    if (Test-Path $logPath) {
        $reportText = Get-Content $logPath -Raw
    }

    $type = if ($reportText -match 'type=(\d+)') { $matches[1] } else { 'NA' }
    $bytes = if ($reportText -match 'total_bytes=(\d+)') { $matches[1] } else { 'NA' }
    $kbits = if ($reportText -match 'kbits_per_s=(\d+)') { $matches[1] } else { 'NA' }
    $reportFound = [bool]($reportText -match 'iperf report:')

    $summary.Add([pscustomobject]@{
        round = $round
        ok = $ok
        reportFound = $reportFound
        type = $type
        bytes = $bytes
        kbits = $kbits
        log = $logPath
        error = $errorText
    })
}

$summary | Format-Table -AutoSize

Write-Host "Rounds: $Rounds"
Write-Host "FailedRounds: $failedRounds"
Write-Host "OutputDir: $OutputDir"

if ($failedRounds -gt 0) {
    exit 1
}
