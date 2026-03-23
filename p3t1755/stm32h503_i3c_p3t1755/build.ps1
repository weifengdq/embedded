[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'clean', 'distclean', 'rebuild', 'gen', 'flash', 'help')]
    [string]$Action = 'build',

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Preset = 'Debug',

    [string]$ProjectName = 'stm32h503_i3c_p3t1755',

    [string]$BuildDir,
    [string]$ElfPath,

    [string]$CubeProgrammerCli,
    [string]$STLinkSN,
    [int]$SWDFreqKHz = 4000,
    [ValidateSet('hex', 'bin')]
    [string]$FlashFormat = 'hex',
    [string]$FlashAddress = '0x08000000',

    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

function Write-Info([string]$msg) { Write-Host "[INFO] $msg" }

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "找不到命令: $name。请先安装并加入 PATH。"
    }
}

function Resolve-DefaultBuildDir([string]$presetName) {
    if ($BuildDir) {
        if ([System.IO.Path]::IsPathRooted($BuildDir)) {
            return $BuildDir
        }

        return (Join-Path -Path $PSScriptRoot -ChildPath $BuildDir)
    }

    return (Join-Path -Path $PSScriptRoot -ChildPath (Join-Path 'build' $presetName))
}

function Invoke-CMakeConfigure([string]$presetName) {
    Require-Command cmake

    $helpText = (& cmake --help 2>$null | Out-String)
    if ($helpText -match '--fresh') {
        & cmake --preset $presetName --fresh
    } else {
        & cmake --preset $presetName
    }
}

function Invoke-CMakeBuild([string]$presetName) {
    Require-Command cmake

    $args = @('--build', '--preset', $presetName)
    if ($Jobs -gt 0) {
        $args += @('--parallel', $Jobs)
    }

    & cmake @args
}

function Invoke-CMakeClean([string]$presetName) {
    Require-Command cmake

    try {
        & cmake --build --preset $presetName --target clean
    } catch {
        $dir = Resolve-DefaultBuildDir $presetName
        if (Test-Path -LiteralPath $dir) {
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
}

function Get-ArtifactPaths([string]$presetName) {
    $dir = Resolve-DefaultBuildDir $presetName

    $elf = if ($ElfPath) { (Resolve-Path -LiteralPath $ElfPath).Path } else { (Join-Path $dir "$ProjectName.elf") }
    $hex = Join-Path $dir "$ProjectName.hex"
    $bin = Join-Path $dir "$ProjectName.bin"

    return [pscustomobject]@{
        BuildDir = $dir
        Elf      = $elf
        Hex      = $hex
        Bin      = $bin
    }
}

$TOOLCHAIN_BIN = 'C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin'
$OBJCOPY = Join-Path $TOOLCHAIN_BIN 'arm-none-eabi-objcopy.exe'
$SIZE    = Join-Path $TOOLCHAIN_BIN 'arm-none-eabi-size.exe'

function Generate-BinHex([string]$elf, [string]$hex, [string]$bin) {
    if (-not (Test-Path -LiteralPath $OBJCOPY)) {
        throw "找不到 objcopy: $OBJCOPY"
    }

    if (-not (Test-Path -LiteralPath $elf)) {
        throw "找不到 ELF: $elf。请先执行 build。"
    }

    Write-Info "生成 $hex"
    & $OBJCOPY -O ihex $elf $hex

    Write-Info "生成 $bin"
    & $OBJCOPY -O binary $elf $bin

    if (Test-Path -LiteralPath $SIZE) {
        & $SIZE $elf
    }
}

function Resolve-CubeProgrammerCli() {
    if ($CubeProgrammerCli) {
        return (Resolve-Path -LiteralPath $CubeProgrammerCli).Path
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles 'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')
    )

    foreach ($p in $candidates) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }

    $cmd = Get-Command STM32_Programmer_CLI.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    return $null
}

function Flash-WithCubeProgrammer([string]$cliPath, [string]$fileToFlash) {
    if (-not (Test-Path -LiteralPath $cliPath)) {
        throw "找不到 STM32CubeProgrammer CLI: $cliPath"
    }
    if (-not (Test-Path -LiteralPath $fileToFlash)) {
        throw "找不到待下载文件: $fileToFlash"
    }

    $connect = "port=SWD freq=$SWDFreqKHz"
    if ($STLinkSN) {
        $connect = "$connect sn=$STLinkSN"
    }

    Write-Info "使用 STM32CubeProgrammer 下载: $fileToFlash"

    if ($FlashFormat -eq 'bin') {
        & $cliPath -c $connect -w $fileToFlash $FlashAddress -v -rst
    } else {
        & $cliPath -c $connect -w $fileToFlash -v -rst
    }
}

function Print-Help {
    @"
用法:
  .\build.ps1 [Action] [Preset] [options]

Action:
  build      配置 + 编译
  clean      清理当前 preset
  distclean  删除当前 preset 输出目录
  rebuild    clean + build
  gen        从 .elf 生成 .hex/.bin
  flash      gen + 下载到开发板
  help       显示帮助

示例:
  .\build.ps1 build Debug
  .\build.ps1 rebuild Debug
  .\build.ps1 flash Debug

依赖:
  - cmake
  - ninja
  - arm-none-eabi-gcc 工具链
  - STM32CubeProgrammer CLI
"@ | Write-Host
}

if ($Action -eq 'help') {
    Print-Help
    exit 0
}

$art = Get-ArtifactPaths $Preset

switch ($Action) {
    'build' {
        Write-Info "Configure ($Preset)"
        Invoke-CMakeConfigure $Preset
        Write-Info "Build ($Preset)"
        Invoke-CMakeBuild $Preset
    }
    'clean' {
        Write-Info "Clean ($Preset)"
        Invoke-CMakeClean $Preset
    }
    'distclean' {
        $dir = $art.BuildDir
        Write-Info "Remove $dir"
        if (Test-Path -LiteralPath $dir) {
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
    'rebuild' {
        Write-Info "Clean ($Preset)"
        Invoke-CMakeClean $Preset
        Write-Info "Configure ($Preset)"
        Invoke-CMakeConfigure $Preset
        Write-Info "Build ($Preset)"
        Invoke-CMakeBuild $Preset
    }
    'gen' {
        Generate-BinHex -elf $art.Elf -hex $art.Hex -bin $art.Bin
    }
    'flash' {
        Generate-BinHex -elf $art.Elf -hex $art.Hex -bin $art.Bin

        $cli = Resolve-CubeProgrammerCli
        if (-not $cli) {
            throw '未找到 STM32CubeProgrammer CLI。请安装 STM32CubeProgrammer，或用 -CubeProgrammerCli 指定路径。'
        }

        $toFlash = if ($FlashFormat -eq 'bin') { $art.Bin } else { $art.Hex }
        Flash-WithCubeProgrammer -cliPath $cli -fileToFlash $toFlash
    }
    default {
        throw "未知 Action: $Action"
    }
}