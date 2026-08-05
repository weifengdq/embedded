<#
.SYNOPSIS
    tc364_can_x8_gcc CMake 命令行构建脚本（TriCore GCC + TASKING 双工具链）。

.PARAMETER Action
    执行操作: configure | build（默认）| rebuild | clean | download | reset | all

.PARAMETER Compiler
    工具链: gcc（默认）| tasking

.PARAMETER BuildType
    CMake 构建类型: Debug（默认）| Release | RelWithDebInfo | MinSizeRel

.PARAMETER AurixStudioPath
    AURIX Studio 安装根目录。脚本从此路径自动推导 tricore-gcc11 bin、make.exe、AURIXFlasher.exe。
    默认: C:\Infineon\AURIX-Studio-1.10.28

.PARAMETER TaskingPath
    商业版 TASKING TriCore 编译器安装根目录（含 ctc\bin\cctc.exe）。
    脚本从此路径自动推导 TASKING bin 目录。
    默认: C:\z\app\TASKING\TriCore_v6.3r1

.PARAMETER GccBin
    显式指定 GCC bin 目录。留空则从 AurixStudioPath 自动推导。

.PARAMETER TaskingBin
    显式指定 TASKING ctc\bin 目录。留空则从 TaskingPath 自动推导。

.PARAMETER FlashTool
    显式指定烧录工具路径。留空则在 AurixStudioPath 内自动查找 AURIXFlasher.exe。

.PARAMETER DasPort
    DAP/DAS 端口索引，默认 0（第一个下载器）。

.EXAMPLE
    .\build.ps1                                        # GCC Debug 编译
    .\build.ps1 -Action download                       # GCC Debug 编译并烧录
    .\build.ps1 -Action reset                          # 仅复位板子
    .\build.ps1 -Compiler tasking                      # TASKING Debug 编译
    .\build.ps1 -Compiler tasking -Action download     # TASKING 编译并烧录
    .\build.ps1 -BuildType Release                     # GCC Release 编译
    .\build.ps1 -Action rebuild -Compiler tasking      # TASKING 清理重编
    .\build.ps1 -Action all                            # GCC 清理重编 + 烧录
    # 新机器换路径:
    .\build.ps1 -AurixStudioPath "C:\Infineon\AURIX-Studio-1.10.32"
    .\build.ps1 -Compiler tasking -TaskingPath "C:\TASKING\TriCore_v6.4r1"
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

    [string]$TargetName = "tc364_can_x8_gcc",

    # ---- 高层路径（推导 GCC/TASKING/Flasher/make 子路径）----
    [string]$AurixStudioPath = "C:\Infineon\AURIX-Studio-1.10.28",
    [string]$TaskingPath     = "C:\z\app\TASKING\TriCore_v6.3r1",

    # ---- 细粒度覆盖（留空则从高层路径自动推导）----
    [string]$GccBin    = "",
    [string]$TaskingBin = "",
    [string]$FlashTool  = "",

    [int]$DasPort = 0,

    [Alias("h")]
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# ------------------------------------------------------------------
# -h / --help：打印帮助后退出
# ------------------------------------------------------------------
if ($Help) {
    $helpText = @"

tc364_can_x8_gcc 构建脚本（TriCore GCC + TASKING 双工具链）

用法:
  .\build.ps1 [选项]

动作 (-Action):
  configure  仅生成构建系统（CMake 配置）
  build      配置 + 编译（默认）
  rebuild    清理构建目录后重新编译
  clean      删除构建目录
  download   编译并烧录到目标板（AURIXFlasher + DAP miniWiggler）
  reset      仅复位目标板
  all        清理重编 + 烧录

工具链 (-Compiler):
  gcc       TriCore GCC（tricore-gcc11，默认）
  tasking   TASKING TriCore v6.3r1

构建类型 (-BuildType):
  Debug（默认） | Release | RelWithDebInfo | MinSizeRel

常用选项:
  -BuildDir <路径>        覆盖构建目录（默认 build/<compiler>）
  -TargetName <名称>      覆盖输出目标名（默认 tc364_can_x8_gcc）
  -AurixStudioPath <路径> AURIX Studio 安装根目录
                          （默认 C:\Infineon\AURIX-Studio-1.10.28）
  -TaskingPath <路径>     TASKING 安装根目录
                          （默认 C:\z\app\TASKING\TriCore_v6.3r1）
  -GccBin <路径>          显式指定 GCC bin 目录（覆盖推导）
  -TaskingBin <路径>      显式指定 TASKING ctc\bin 目录（覆盖推导）
  -FlashTool <路径>       显式指定烧录工具（AURIXFlasher.exe）
  -DasPort <索引>         DAP/DAS 端口索引（默认 0）
  -Help / -h              显示本帮助

示例:
  .\build.ps1                                  # GCC Debug 编译
  .\build.ps1 -Action download                 # GCC 编译并烧录
  .\build.ps1 -Compiler tasking -Action all     # TASKING 清理重编+烧录
  .\build.ps1 -AurixStudioPath "C:\Infineon\AURIX-Studio-1.10.32"
  .\build.ps1 -Compiler tasking -TaskingPath "C:\TASKING\TriCore_v6.4r1"
"@
    Write-Host $helpText
    exit 0
}

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

# ------------------------------------------------------------------
# 推导各工具路径
# ------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($GccBin)) {
    $GccBin = Join-Path $AurixStudioPath "tools\Compilers\tricore-gcc11\bin"
}
if ([string]::IsNullOrWhiteSpace($TaskingBin)) {
    $TaskingBin = Join-Path $TaskingPath "ctc\bin"
}

# make.exe：优先使用 AURIX Studio 自带的，其次 PATH
$aurixMake = Join-Path $AurixStudioPath "tools\make\make.exe"
if (-not (Test-Path $aurixMake)) {
    $sysMake = Get-Command make.exe -ErrorAction SilentlyContinue
    $aurixMake = if ($sysMake) { $sysMake.Source } else { $null }
}

# AURIXFlasher.exe：在 AurixStudioPath\tools 下搜索版本目录，取最新版
$flasherSearchRoot = Join-Path $AurixStudioPath "tools"
$flasherDir = Get-ChildItem $flasherSearchRoot -Filter "AurixFlasherSoftwareTool*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
$defaultAurixFlasher = if ($flasherDir) {
    Join-Path $flasherDir.FullName "AURIXFlasher.exe"
} else {
    Join-Path $AurixStudioPath "tools\AurixFlasherSoftwareTool_v3.0.14\AURIXFlasher.exe"
}

# ------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = "build/$Compiler"
}
$buildPath = Join-Path $projectRoot $BuildDir

$toolchainFile = if ($Compiler -eq "tasking") {
    Join-Path $projectRoot "cmake\tasking-tricore-toolchain.cmake"
} else {
    Join-Path $projectRoot "cmake\tricore-gcc-toolchain.cmake"
}

$elfPath      = Join-Path $buildPath ("{0}.elf"            -f $TargetName)
$hexPath      = Join-Path $buildPath ("{0}.hex"            -f $TargetName)
$flashLogPath = Join-Path $buildPath ("{0}_flasher_log.xml" -f $TargetName)

# ------------------------------------------------------------------
function Invoke-Step {
    param(
        [Parameter(Mandatory)] [string]   $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList,
        [switch] $IgnoreExitCode
    )
    Write-Host ("> {0} {1}" -f $FilePath, ($ArgumentList -join " "))
    & $FilePath @ArgumentList
    $exitCode = $LASTEXITCODE
    if ((-not $IgnoreExitCode) -and $exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $FilePath"
    }
}

function Invoke-Configure {
    if (-not (Test-Path $toolchainFile)) {
        throw "Toolchain file not found: $toolchainFile"
    }

    if ($Compiler -eq "tasking") {
        if (-not (Test-Path (Join-Path $TaskingBin "cctc.exe"))) {
            throw "TASKING cctc.exe not found in: $TaskingBin`nSet -TaskingPath or -TaskingBin."
        }
        $configureArgs = @(
            "-S", $projectRoot,
            "-B", $buildPath,
            "-G", "Ninja",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
            "-DAURIX_TASKING_BIN=$TaskingBin",
            "-DCMAKE_BUILD_TYPE=$BuildType"
        )
    } else {
        if (-not (Test-Path (Join-Path $GccBin "tricore-elf-gcc.exe"))) {
            throw "tricore-elf-gcc.exe not found in: $GccBin`nSet -AurixStudioPath or -GccBin."
        }
        $configureArgs = @(
            "-S", $projectRoot,
            "-B", $buildPath,
            "-G", "Ninja",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
            "-DAURIX_TOOLCHAIN_BIN=$GccBin",
            "-DCMAKE_BUILD_TYPE=$BuildType"
        )
    }

    Invoke-Step -FilePath "cmake" -ArgumentList $configureArgs
}

function Invoke-Build {
    Invoke-Configure
    $logicalCores = (Get-CimInstance Win32_Processor |
        Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if (-not $logicalCores) { $logicalCores = 4 }
    Invoke-Step -FilePath "cmake" -ArgumentList @(
        "--build", $buildPath, "--config", $BuildType, "--", "-j$logicalCores"
    )
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
    if (-not [string]::IsNullOrWhiteSpace($FlashTool)) { return $FlashTool }
    if (Test-Path $defaultAurixFlasher) { return $defaultAurixFlasher }
    if ($env:AURIX_DOWNLOAD_TOOL -and (Test-Path $env:AURIX_DOWNLOAD_TOOL)) {
        return $env:AURIX_DOWNLOAD_TOOL
    }
    throw "AURIXFlasher.exe not found under: $AurixStudioPath`nSet -AurixStudioPath or -FlashTool."
}

function Invoke-Download {
    if (-not (Test-Path $hexPath)) { Invoke-Build }
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
    if (Test-Path $flashLogPath) { Write-Host "Flasher log  : $flashLogPath" }
}

function Invoke-Reset {
    $flasherExe = Resolve-FlashTool
    Write-Host "Resetting device via AURIXFlasher..."
    Write-Host "AURIXFlasher : $flasherExe"
    Write-Host "DAS port     : $DasPort"
    # 复位脚本写到构建目录而非系统 TEMP：部分环境下 AURIXFlasher 读取
    # C:\WINDOWS\TEMP 下的文件会报"拒绝访问"。
    $scriptContent = "INIT`r`nRESET 6`r`n"
    if (-not (Test-Path $buildPath)) {
        New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
    }
    $tmpScript = Join-Path $buildPath "aurix_reset.txt"
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

# ------------------------------------------------------------------
Write-Host ("=== {0} | Compiler={1} | Action={2} | BuildType={3} ===" -f $TargetName, $Compiler, $Action, $BuildType) -ForegroundColor Cyan
Write-Host "  GccBin     : $GccBin"
Write-Host "  TaskingBin : $TaskingBin"
Write-Host "  Flasher    : $defaultAurixFlasher"

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
