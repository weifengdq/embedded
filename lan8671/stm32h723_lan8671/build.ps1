[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'clean', 'distclean', 'rebuild', 'gen', 'flash', 'help')]
    [string]$Action = 'build',

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Preset = 'Debug',

    [string]$ProjectName = 'stm32h723_lan8671',
    [string]$BuildDir,
    [string]$ElfPath,
    [string]$ToolchainBinDir,
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
        throw "Required command not found: $Name"
    }
}

function Resolve-ToolchainBinDir() {
    if ($ToolchainBinDir) {
        return (Resolve-Path -LiteralPath $ToolchainBinDir).Path
    }

    if ($env:ARM_NONE_EABI_TOOLCHAIN_BIN) {
        return (Resolve-Path -LiteralPath $env:ARM_NONE_EABI_TOOLCHAIN_BIN).Path
    }

    $candidates = @(
        'C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Resolve-DefaultBuildDir([string]$PresetName) {
    if ($BuildDir) {
        if ([System.IO.Path]::IsPathRooted($BuildDir)) {
            return $BuildDir
        }

        return (Join-Path $PSScriptRoot $BuildDir)
    }

    return (Join-Path $PSScriptRoot (Join-Path 'build' $PresetName))
}

function Invoke-CMakeConfigure([string]$PresetName) {
    Require-Command cmake

    $helpText = (& cmake --help 2>$null | Out-String)
    $cmakeArgs = @('--preset', $PresetName)
    $resolvedToolchainBinDir = Resolve-ToolchainBinDir

    if ($helpText -match '--fresh') {
        $cmakeArgs += '--fresh'
    }

    if ($resolvedToolchainBinDir) {
        $cmakeArgs += "-DARM_NONE_EABI_TOOLCHAIN_BIN=$resolvedToolchainBinDir"
    }

    & cmake @cmakeArgs
}

function Invoke-CMakeBuild([string]$PresetName) {
    Require-Command cmake

    $buildArgs = @('--build', '--preset', $PresetName)
    if ($Jobs -gt 0) {
        $buildArgs += @('--parallel', $Jobs)
    }

    & cmake @buildArgs
}

function Invoke-CMakeClean([string]$PresetName) {
    Require-Command cmake

    try {
        & cmake --build --preset $PresetName --target clean
    }
    catch {
        $resolvedBuildDir = Resolve-DefaultBuildDir $PresetName
        if (Test-Path -LiteralPath $resolvedBuildDir) {
            Remove-Item -LiteralPath $resolvedBuildDir -Recurse -Force
        }
    }
}

function Get-ArtifactPaths([string]$PresetName) {
    $resolvedBuildDir = Resolve-DefaultBuildDir $PresetName
    $resolvedElf = if ($ElfPath) {
        (Resolve-Path -LiteralPath $ElfPath).Path
    }
    else {
        Join-Path $resolvedBuildDir "$ProjectName.elf"
    }

    return [pscustomobject]@{
        BuildDir = $resolvedBuildDir
        Elf      = $resolvedElf
        Hex      = (Join-Path $resolvedBuildDir "$ProjectName.hex")
        Bin      = (Join-Path $resolvedBuildDir "$ProjectName.bin")
    }
}

function Resolve-GnuTool([string]$ToolName) {
    $resolvedToolchainBinDir = Resolve-ToolchainBinDir
    if ($resolvedToolchainBinDir) {
        $candidate = Join-Path $resolvedToolchainBinDir $ToolName
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Generate-BinHex([string]$Elf, [string]$Hex, [string]$Bin) {
    $objcopy = Resolve-GnuTool 'arm-none-eabi-objcopy.exe'
    $sizeTool = Resolve-GnuTool 'arm-none-eabi-size.exe'

    if (-not $objcopy) {
        throw 'arm-none-eabi-objcopy.exe was not found.'
    }

    if (-not (Test-Path -LiteralPath $Elf)) {
        throw "ELF not found: $Elf"
    }

    Write-Info "Generating $Hex"
    & $objcopy -O ihex $Elf $Hex

    Write-Info "Generating $Bin"
    & $objcopy -O binary $Elf $Bin

    if ($sizeTool) {
        & $sizeTool $Elf
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

    $command = Get-Command STM32_Programmer_CLI.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Flash-WithCubeProgrammer([string]$CliPath, [string]$FileToFlash) {
    if (-not (Test-Path -LiteralPath $CliPath)) {
        throw "STM32CubeProgrammer CLI not found: $CliPath"
    }

    if (-not (Test-Path -LiteralPath $FileToFlash)) {
        throw "Flash image not found: $FileToFlash"
    }

    $connect = "port=SWD freq=$SWDFreqKHz"
    if ($STLinkSN) {
        $connect = "$connect sn=$STLinkSN"
    }

    Write-Info "Flashing $FileToFlash"
    if ($FlashFormat -eq 'bin') {
        & $CliPath -c $connect -w $FileToFlash $FlashAddress -v -rst
    }
    else {
        & $CliPath -c $connect -w $FileToFlash -v -rst
    }
}

function Print-Help {
    @"
Usage:
  .\build.ps1 [Action] [Preset] [options]

Action:
  build      Configure and build (default)
  clean      Clean current preset
  distclean  Remove the preset build directory
  rebuild    Clean and build again
  gen        Generate .hex and .bin from the ELF
  flash      Generate images and flash with STM32CubeProgrammer
  help       Show this help

Examples:
  .\build.ps1 build Debug
  .\build.ps1 rebuild Release
  .\build.ps1 gen Debug
  .\build.ps1 flash Debug
    .\build.ps1 build Debug -ToolchainBinDir C:\Toolchains\gcc-arm-none-eabi\bin
  .\build.ps1 flash Debug -STLinkSN 066CFF... -SWDFreqKHz 8000
    .\build.ps1 flash Debug -ToolchainBinDir C:\Toolchains\gcc-arm-none-eabi\bin -CubeProgrammerCli C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe

Requirements:
  - cmake with preset support
  - ninja
  - arm-none-eabi toolchain
  - STM32CubeProgrammer for flash
"@ | Write-Host
}

Push-Location $PSScriptRoot
try {
    if ($Action -eq 'help') {
        Print-Help
        exit 0
    }

    $artifacts = Get-ArtifactPaths $Preset

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
            Write-Info "Remove $($artifacts.BuildDir)"
            if (Test-Path -LiteralPath $artifacts.BuildDir) {
                Remove-Item -LiteralPath $artifacts.BuildDir -Recurse -Force
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
            Generate-BinHex -Elf $artifacts.Elf -Hex $artifacts.Hex -Bin $artifacts.Bin
        }
        'flash' {
            Generate-BinHex -Elf $artifacts.Elf -Hex $artifacts.Hex -Bin $artifacts.Bin

            $cli = Resolve-CubeProgrammerCli
            if (-not $cli) {
                throw 'STM32CubeProgrammer CLI was not found.'
            }

            $fileToFlash = if ($FlashFormat -eq 'bin') { $artifacts.Bin } else { $artifacts.Hex }
            Flash-WithCubeProgrammer -CliPath $cli -FileToFlash $fileToFlash
        }
        default {
            throw "Unknown action: $Action"
        }
    }
}
finally {
    Pop-Location
}