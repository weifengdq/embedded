[CmdletBinding()]
param(
    [ValidateSet("configure", "build", "rebuild", "clean", "download", "all")]
    [string]$Action = "build",

    [string]$BuildDir = "build",

    [string]$Generator = "Ninja",

    [string]$ToolchainBin = "C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin",

    [string]$TargetName = "tc4d7_gpio",

    [string]$FlashTool = "",

    [string[]]$FlashArgs = @()
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $projectRoot $BuildDir
$toolchainFile = Join-Path $projectRoot "cmake/tricore-gcc-toolchain.cmake"
$elfPath = Join-Path $buildPath ("{0}.elf" -f $TargetName)
$hexPath = Join-Path $buildPath ("{0}.hex" -f $TargetName)

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
        "-DAURIX_TOOLCHAIN_BIN=$ToolchainBin"
    )

    Invoke-Step -FilePath "cmake" -ArgumentList $configureArgs
}

function Build-Project {
    Ensure-Configured
    Invoke-Step -FilePath "cmake" -ArgumentList @("--build", $buildPath)
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
        if(-not [string]::IsNullOrWhiteSpace($env:AURIX_DOWNLOAD_TOOL)) {
            $script:FlashTool = $env:AURIX_DOWNLOAD_TOOL
        }
        else {
            throw "No download tool configured. Use -FlashTool and -FlashArgs, or set AURIX_DOWNLOAD_TOOL and AURIX_DOWNLOAD_ARGS. Tokens: {hex} {elf} {buildDir} {projectDir}."
        }
    }

    if($FlashArgs.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($env:AURIX_DOWNLOAD_ARGS)) {
        $script:FlashArgs = [System.Management.Automation.PSParser]::Tokenize($env:AURIX_DOWNLOAD_ARGS, [ref]$null) |
            Where-Object { $_.Type -eq "String" } |
            ForEach-Object { $_.Content }
    }

    $resolvedFlashArgs = foreach($argument in $FlashArgs) {
        $argument.Replace("{hex}", $hexPath).Replace("{elf}", $elfPath).Replace("{buildDir}", $buildPath).Replace("{projectDir}", $projectRoot)
    }

    Invoke-Step -FilePath $FlashTool -ArgumentList $resolvedFlashArgs
}

switch($Action) {
    "configure" { Configure-Project }
    "build"     { Build-Project }
    "rebuild"   { Clean-Project; Configure-Project; Build-Project }
    "clean"     { Clean-Project }
    "download"  { Download-Project }
    "all"       { Configure-Project; Build-Project; Download-Project }
}