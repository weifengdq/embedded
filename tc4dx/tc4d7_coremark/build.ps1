[CmdletBinding()]
param(
    [ValidateSet("configure", "build", "rebuild", "clean", "download", "all")]
    [string]$Action = "build",

    [string]$BuildDir = "build",

    [string]$Generator = "Ninja",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "Release",

    [string]$ToolchainBin = "C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin",

    [string]$TargetName = "tc4d7_coremark",

    [ValidateRange(1, 6)]
    [int]$CoremarkThreads = 1,

    [ValidateSet("STACK", "MALLOC", "STATIC")]
    [string]$CoremarkMemMethod = "STACK",

    [int]$CoremarkIterations = 0,

    [string]$FlashTool = "",

    [string[]]$FlashArgs = @()
)

$ErrorActionPreference = "Stop"

$defaultAurixFlasher = "C:\Infineon\AURIX-Studio-1.10.28\tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $projectRoot $BuildDir
$toolchainFile = Join-Path $projectRoot "cmake/tricore-gcc-toolchain.cmake"
$elfPath = Join-Path $buildPath ("{0}.elf" -f $TargetName)
$hexPath = Join-Path $buildPath ("{0}.hex" -f $TargetName)
$flashLogPath = Join-Path $buildPath ("{0}_flasher_log.xml" -f $TargetName)

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList,

        [switch]$IgnoreExitCode
    )

    Write-Host ("> {0} {1}" -f $FilePath, ($ArgumentList -join " "))
    & $FilePath @ArgumentList
    $exitCode = $LASTEXITCODE
    if((-not $IgnoreExitCode) -and $exitCode -ne 0) {
        throw "Command failed with exit code $exitCode"
    }
}

function Ensure-Configured {
    $cacheFile = Join-Path $buildPath "CMakeCache.txt"
    if(-not (Test-Path $cacheFile)) {
        Configure-Project
    }
}

function Configure-Project {
    if(-not (Test-Path $toolchainFile)) {
        throw "Toolchain file not found: $toolchainFile"
    }

    $configureArgs = @(
        "-S", $projectRoot,
        "-B", $buildPath,
        "-G", $Generator,
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
        "-DAURIX_TOOLCHAIN_BIN=$ToolchainBin",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCOREMARK_MULTITHREAD=$CoremarkThreads",
        "-DCOREMARK_MEM_METHOD=$CoremarkMemMethod",
        "-DCOREMARK_ITERATIONS=$CoremarkIterations"
    )

    Invoke-Step -FilePath "cmake" -ArgumentList $configureArgs
}

function Build-Project {
    Configure-Project
    Invoke-Step -FilePath "cmake" -ArgumentList @("--build", $buildPath, "--config", $BuildType)
}

function Clean-Project {
    if(Test-Path $buildPath) {
        Write-Host ("> Remove-Item -Recurse -Force {0}" -f $buildPath)
        Remove-Item -Recurse -Force $buildPath
    }
}

function Download-Project {
    Ensure-Configured

    if(-not (Test-Path $hexPath)) {
        Build-Project
    }

    if([string]::IsNullOrWhiteSpace($FlashTool)) {
        if(Test-Path $defaultAurixFlasher) {
            $script:FlashTool = $defaultAurixFlasher
        }
        elseif(-not [string]::IsNullOrWhiteSpace($env:AURIX_DOWNLOAD_TOOL)) {
            $script:FlashTool = $env:AURIX_DOWNLOAD_TOOL
        }
        else {
            throw "No download tool configured. Install AURIXFlasher.exe at the default path, or use -FlashTool and -FlashArgs, or set AURIX_DOWNLOAD_TOOL and AURIX_DOWNLOAD_ARGS. Tokens: {hex} {elf} {buildDir} {projectDir}."
        }
    }

    if(-not (Test-Path $FlashTool)) {
        throw "Download tool not found: $FlashTool"
    }

    if($FlashArgs.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($env:AURIX_DOWNLOAD_ARGS)) {
        $script:FlashArgs = [System.Management.Automation.PSParser]::Tokenize($env:AURIX_DOWNLOAD_ARGS, [ref]$null) |
            Where-Object { $_.Type -eq "String" } |
            ForEach-Object { $_.Content }
    }

    if($FlashArgs.Count -eq 0 -and [System.IO.Path]::GetFileName($FlashTool) -ieq "AURIXFlasher.exe") {
        $script:FlashArgs = @(
            "-hex", "{hex}",
            "-connect", "6",
            "-start", "on",
            "-log", "{buildDir}/${TargetName}_flasher_log.xml"
        )
    }

    if($FlashArgs.Count -eq 0) {
        throw "No download arguments configured for $FlashTool"
    }

    $resolvedFlashArgs = foreach($argument in $FlashArgs) {
        $argument.Replace("{hex}", $hexPath).Replace("{elf}", $elfPath).Replace("{buildDir}", $buildPath).Replace("{projectDir}", $projectRoot)
    }

    Invoke-Step -FilePath $FlashTool -ArgumentList $resolvedFlashArgs

    if(Test-Path $flashLogPath) {
        Write-Host ("Flasher XML log: {0}" -f $flashLogPath)
    }
}

switch($Action) {
    "configure" { Configure-Project }
    "build"     { Build-Project }
    "rebuild"   { Clean-Project; Configure-Project; Build-Project }
    "clean"     { Clean-Project }
    "download"  { Download-Project }
    "all"       { Configure-Project; Build-Project; Download-Project }
}