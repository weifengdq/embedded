[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'clean', 'distclean', 'rebuild', 'gen', 'flash', 'help')]
    [string]$Action = 'build',

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Preset = 'Debug',

    [string]$ProjectName = 'stm32f407_lan8671',
    [string]$BuildDir,
    [string]$ElfPath,
    [string]$ToolchainBin,
    [string]$CubeProgrammerCli,
    [string]$STLinkSN,
    [int]$SWDFreqKHz = 4000,
    [ValidateSet('hex', 'bin')]
    [string]$FlashFormat = 'hex',
    [string]$FlashAddress = '0x08000000',
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

function Write-Info([string]$Message) {
    Write-Host "[INFO] $Message"
}

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "找不到命令: $Name。请先安装并加入 PATH。"
    }
}

function Resolve-ExistingPath([string]$PathValue) {
    if (-not $PathValue) {
        return $null
    }

    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Resolve-DefaultBuildDir([string]$PresetName) {
    if ($BuildDir) {
        if ([System.IO.Path]::IsPathRooted($BuildDir)) {
            return $BuildDir
        }

        return (Join-Path -Path $PSScriptRoot -ChildPath $BuildDir)
    }

    return (Join-Path -Path $PSScriptRoot -ChildPath (Join-Path 'build' $PresetName))
}

function Invoke-CMakeConfigure([string]$PresetName) {
    Require-Command cmake

    $helpText = (& cmake --help 2>$null | Out-String)
    if ($helpText -match '--fresh') {
        & cmake --preset $PresetName --fresh
    }
    else {
        & cmake --preset $PresetName
    }
}

function Invoke-CMakeBuild([string]$PresetName) {
    Require-Command cmake

    $args = @('--build', '--preset', $PresetName)
    if ($Jobs -gt 0) {
        $args += @('--parallel', $Jobs)
    }

    & cmake @args
}

function Invoke-CMakeClean([string]$PresetName) {
    Require-Command cmake

    try {
        & cmake --build --preset $PresetName --target clean
    }
    catch {
        $dir = Resolve-DefaultBuildDir $PresetName
        if (Test-Path -LiteralPath $dir) {
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
}

function Get-ArtifactPaths([string]$PresetName) {
    $dir = Resolve-DefaultBuildDir $PresetName
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

function Resolve-ToolFromBin([string]$ToolName, [string]$FileName) {
    if ($ToolchainBin) {
        $candidate = Join-Path (Resolve-ExistingPath $ToolchainBin) $FileName
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command $FileName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $cmd = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    return $null
}

function Generate-BinHex([string]$elf, [string]$hex, [string]$bin) {
    $objcopy = Resolve-ToolFromBin 'arm-none-eabi-objcopy' 'arm-none-eabi-objcopy.exe'
    $size = Resolve-ToolFromBin 'arm-none-eabi-size' 'arm-none-eabi-size.exe'

    if (-not $objcopy) {
        throw '找不到 arm-none-eabi-objcopy。请确认 GCC Arm Embedded 工具链已加入 PATH，或通过 -ToolchainBin 指定 bin 目录。'
    }

    if (-not (Test-Path -LiteralPath $elf)) {
        throw "找不到 ELF: $elf。请先执行 build。"
    }

    Write-Info "生成 $hex"
    & $objcopy -O ihex $elf $hex

    Write-Info "生成 $bin"
    & $objcopy -O binary $elf $bin

    if ($size) {
        & $size $elf
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

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    $cmd = Get-Command STM32_Programmer_CLI.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    return $null
}

function Flash-WithCubeProgrammer([string]$CliPath, [string]$FileToFlash) {
    if (-not (Test-Path -LiteralPath $CliPath)) {
        throw "找不到 STM32CubeProgrammer CLI: $CliPath"
    }
    if (-not (Test-Path -LiteralPath $FileToFlash)) {
        throw "找不到待下载文件: $FileToFlash"
    }

    $connect = "port=SWD freq=$SWDFreqKHz"
    if ($STLinkSN) {
        $connect = "$connect sn=$STLinkSN"
    }

    Write-Info "使用 STM32CubeProgrammer 下载: $FileToFlash"

    if ($FlashFormat -eq 'bin') {
        & $CliPath -c $connect -w $FileToFlash $FlashAddress -v -rst
    }
    else {
        & $CliPath -c $connect -w $FileToFlash -v -rst
    }
}

function Print-Help {
@"
用法:
  .\build.ps1 [Action] [Preset] [options]

Action:
  build      配置 + 编译
  clean      清理当前 preset
  distclean  删除当前 preset 的构建目录
  rebuild    clean + build
  gen        从 .elf 生成 .hex/.bin
  flash      gen + 烧录固件
  help       显示帮助

示例:
  .\build.ps1 rebuild Release
  .\build.ps1 flash Debug

可选参数:
  -ToolchainBin       GCC Arm Embedded 的 bin 目录
  -CubeProgrammerCli  STM32CubeProgrammer CLI 路径
  -STLinkSN           指定 ST-Link 序列号
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
        Write-Info "Build ($Preset)"
        Invoke-CMakeBuild $Preset
        Generate-BinHex -elf $art.Elf -hex $art.Hex -bin $art.Bin

        $cli = Resolve-CubeProgrammerCli
        if (-not $cli) {
            throw '未找到 STM32CubeProgrammer CLI。请安装 STM32CubeProgrammer，或用 -CubeProgrammerCli 指定路径。'
        }

        $toFlash = if ($FlashFormat -eq 'bin') { $art.Bin } else { $art.Hex }
        Flash-WithCubeProgrammer -CliPath $cli -FileToFlash $toFlash
    }
    default {
        throw "未知 Action: $Action"
    }
}