<#
.SYNOPSIS
    tc387_blink_gcc_test CMake 命令行构建脚本。

.DESCRIPTION
    支持 TriCore GCC（默认）和 TASKING 两种工具链，提供配置、构建、清理、下载、复位等操作。

.PARAMETER Action
    执行的操作:
      configure  -- 仅运行 CMake 配置
      build      -- 配置（如需）并编译
      rebuild    -- 清理后重新编译
      clean      -- 删除构建目录
      download   -- 编译（如需）并烧录到目标板
      reset      -- 仅复位目标板（不烧录）
      all        -- 相当于 rebuild + download

.PARAMETER Compiler
    工具链选择: gcc（默认）或 tasking

.PARAMETER BuildType
    CMake 构建类型: Debug（默认）、Release、RelWithDebInfo、MinSizeRel

.PARAMETER BuildDir
    构建目录名（相对于脚本所在目录），默认根据 Compiler 自动设为 build/gcc 或 build/tasking

.PARAMETER TargetName
    编译输出基础文件名，默认 tc387_blink_gcc_test

.PARAMETER GccToolchainBin
    GCC 工具链 bin 目录，默认 C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin

.PARAMETER TaskingBin
    TASKING ctc bin 目录，默认 C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\Tasking_1.1r8\ctc\bin

.PARAMETER FlashTool
    烧录工具路径，默认自动查找 AURIXFlasher.exe

.PARAMETER DasPort
    DAP/DAS 端口索引（0 表示第一个下载器），默认 0

.EXAMPLE
    .\build.ps1                          # GCC Debug 编译
    .\build.ps1 -Action build            # GCC Debug 编译
    .\build.ps1 -Action download         # GCC Debug 编译并烧录
    .\build.ps1 -Action reset            # 仅复位板子
    .\build.ps1 -Compiler tasking        # TASKING Debug 编译
    .\build.ps1 -Compiler tasking -Action download  # TASKING 编译并烧录
    .\build.ps1 -BuildType Release       # GCC Release 编译
    .\build.ps1 -Action rebuild -Compiler tasking   # TASKING 清理重编
#>
[CmdletBinding()]
param(
    [ValidateSet("configure", "build", "rebuild", "clean", "download", "reset", "all")]
    [string]$Action = "build",

    [ValidateSet("gcc", "tasking")]
    [string]$Compiler = "gcc",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "Debug",

    [string]$BuildDir = "",

    [string]$TargetName = "tc387_blink_gcc_test",

    [string]$GccToolchainBin = "C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\tricore-gcc11\bin",

    [string]$TaskingBin = "C:\Infineon\AURIX-Studio-1.10.28\tools\Compilers\Tasking_1.1r8\ctc\bin",

    [string]$FlashTool = "",

    [int]$DasPort = 0
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

# 默认构建目录：build/gcc 或 build/tasking
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = "build/$Compiler"
}
$buildPath = Join-Path $projectRoot $BuildDir

# 工具链文件
if ($Compiler -eq "tasking") {
    $toolchainFile = Join-Path $projectRoot "cmake\tasking-tricore-toolchain.cmake"
} else {
    $toolchainFile = Join-Path $projectRoot "cmake\tricore-gcc-toolchain.cmake"
}

$elfPath     = Join-Path $buildPath ("{0}.elf" -f $TargetName)
$hexPath     = Join-Path $buildPath ("{0}.hex" -f $TargetName)
$flashLogPath = Join-Path $buildPath ("{0}_flasher_log.xml" -f $TargetName)

$defaultAurixFlasher = "C:\Infineon\AURIX-Studio-1.10.28\tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe"

# AURIX Studio 自带的 make.exe（供 Tasking 使用）
$aurixMakeDir = "C:\Infineon\AURIX-Studio-1.10.28\tools\make"

# -----------------------------------------------------------------------
function Invoke-Step {
    param(
        [Parameter(Mandatory)]  [string]   $FilePath,
        [Parameter(Mandatory)]  [string[]] $ArgumentList,
        [switch] $IgnoreExitCode
    )
    Write-Host ("> {0} {1}" -f $FilePath, ($ArgumentList -join " "))
    & $FilePath @ArgumentList
    $exitCode = $LASTEXITCODE
    if ((-not $IgnoreExitCode) -and $exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $FilePath"
    }
}

function Ensure-Configured {
    $cacheFile = Join-Path $buildPath "CMakeCache.txt"
    if (-not (Test-Path $cacheFile)) {
        Invoke-Configure
    }
}

function Invoke-Configure {
    if (-not (Test-Path $toolchainFile)) {
        throw "Toolchain file not found: $toolchainFile"
    }

    if ($Compiler -eq "tasking") {
        # ADS 捆绑版 TASKING（非商业版）仅允许在 IDE 内运行，
        # 命令行独立编译需要商业版 TASKING 许可证。
        Write-Warning "TASKING non-commercial license (ADS bundled) does NOT support standalone CLI builds."
        Write-Warning "You need a commercial TASKING license for command-line compilation."
        Write-Warning "Proceeding anyway - configure will succeed but compile may fail with F104 license error."

        # Tasking 使用 Unix Makefiles，make.exe 来自 AURIX Studio
        $generator = "Unix Makefiles"
        $makeExe   = Join-Path $aurixMakeDir "make.exe"
        if (-not (Test-Path $makeExe)) {
            throw "make.exe not found at: $makeExe"
        }
        $configureArgs = @(
            "-S", $projectRoot,
            "-B", $buildPath,
            "-G", $generator,
            "-DCMAKE_MAKE_PROGRAM=$makeExe",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
            "-DAURIX_TASKING_BIN=$TaskingBin",
            "-DCMAKE_BUILD_TYPE=$BuildType"
        )
    } else {
        # GCC 使用 Ninja
        $generator = "Ninja"
        $configureArgs = @(
            "-S", $projectRoot,
            "-B", $buildPath,
            "-G", $generator,
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
            "-DAURIX_TOOLCHAIN_BIN=$GccToolchainBin",
            "-DCMAKE_BUILD_TYPE=$BuildType"
        )
    }

    Invoke-Step -FilePath "cmake" -ArgumentList $configureArgs
}

function Invoke-Build {
    Invoke-Configure
    $logicalCores = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if (-not $logicalCores) { $logicalCores = 4 }
    Invoke-Step -FilePath "cmake" -ArgumentList @("--build", $buildPath, "--config", $BuildType, "--", "-j$logicalCores")
}

function Invoke-Clean {
    if (Test-Path $buildPath) {
        Write-Host ("> Remove-Item -Recurse -Force {0}" -f $buildPath)
        Remove-Item -Recurse -Force $buildPath
    } else {
        Write-Host "Nothing to clean: $buildPath"
    }
}

function Resolve-FlashTool {
    if (-not [string]::IsNullOrWhiteSpace($FlashTool)) {
        return $FlashTool
    }
    if (Test-Path $defaultAurixFlasher) {
        return $defaultAurixFlasher
    }
    if (-not [string]::IsNullOrWhiteSpace($env:AURIX_DOWNLOAD_TOOL) -and (Test-Path $env:AURIX_DOWNLOAD_TOOL)) {
        return $env:AURIX_DOWNLOAD_TOOL
    }
    throw "AURIXFlasher.exe not found. Expected: $defaultAurixFlasher"
}

function Invoke-Download {
    # 确保有编译产物
    if (-not (Test-Path $hexPath)) {
        Invoke-Build
    }
    if (-not (Test-Path $hexPath)) {
        throw "HEX file not found after build: $hexPath"
    }

    $flasherExe = Resolve-FlashTool

    Write-Host "AURIXFlasher : $flasherExe"
    Write-Host "HEX file     : $hexPath"
    Write-Host "DAS port     : $DasPort"

    Invoke-Step -FilePath $flasherExe -ArgumentList @(
        "-hex",     $hexPath,
        "-id",      $DasPort,
        "-erase",   "on",
        "-prog",    "on",
        "-ver",     "on",
        "-connect", "6",
        "-start",   "on",
        "-log",     $flashLogPath
    )

    if (Test-Path $flashLogPath) {
        Write-Host "Flasher log  : $flashLogPath"
    }
}

function Invoke-Reset {
    $flasherExe = Resolve-FlashTool

    Write-Host "Resetting device via AURIXFlasher..."
    Write-Host "AURIXFlasher : $flasherExe"
    Write-Host "DAS port     : $DasPort"

    # 使用 script 模式仅执行 RESET 6（reset and halt 后再 start）
    $scriptContent = "INIT`r`nRESET 6`r`n"
    $tmpScript = Join-Path ([System.IO.Path]::GetTempPath()) "aurix_reset_$([System.IO.Path]::GetRandomFileName()).txt"
    try {
        [System.IO.File]::WriteAllText($tmpScript, $scriptContent, [System.Text.Encoding]::ASCII)
        Invoke-Step -FilePath $flasherExe -ArgumentList @(
            "-id",      $DasPort,
            "-prog",    "off",
            "-erase",   "off",
            "-script",  $tmpScript
        )
    } finally {
        Remove-Item $tmpScript -ErrorAction SilentlyContinue
    }
}

# -----------------------------------------------------------------------
Write-Host "=== tc387_blink_gcc_test | Compiler=$Compiler | Action=$Action | BuildType=$BuildType ===" -ForegroundColor Cyan

switch ($Action) {
    "configure" { Invoke-Configure }
    "build"     { Invoke-Build }
    "rebuild"   { Invoke-Clean; Invoke-Build }
    "clean"     { Invoke-Clean }
    "download"  { Invoke-Download }
    "reset"     { Invoke-Reset }
    "all"       { Invoke-Clean; Invoke-Build; Invoke-Download }
}

Write-Host "Done." -ForegroundColor Green
