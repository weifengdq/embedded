param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('configure', 'build', 'rebuild', 'flash', 'gdbserver', 'clean')]
    [string]$Action,

    [Parameter(Mandatory = $false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [Parameter(Mandatory = $false)]
    [ValidateSet('A', 'B')]
    [string]$Role = 'A',

    [Parameter(Mandatory = $false)]
    [string]$SdkDir = 'D:\hpm\embedded\hpm5e31\sdk_env_v1.11.0\hpm_sdk',

    [Parameter(Mandatory = $false)]
    [string]$JLinkDir = 'C:\Program Files\SEGGER\JLink_V844',

    [Parameter(Mandatory = $false)]
    [string]$JLinkDevice = 'HPM5E31xGNx',

    [Parameter(Mandatory = $false)]
    [int]$JLinkSpeed = 4000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BoardName = 'hpm5e31_lite'
$RoleName = $Role.ToUpperInvariant()
$BoardSearchPath = Join-Path $ProjectRoot 'boards'
$HpmBuildType = 'flash_xip'
$CmakeBuildType = if ($Config -eq 'Release') { 'release' } else { 'debug' }
$BuildDirSuffix = if ($Config -eq 'Release') { 'release' } else { 'debug' }
$BuildDir = Join-Path $ProjectRoot (Join-Path 'build' ("{0}_{1}_{2}_{3}" -f $BoardName, $HpmBuildType, $BuildDirSuffix, $RoleName.ToLowerInvariant()))

function Get-ValidatedPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path $Path)) {
        throw "$Description not found: $Path"
    }

    return (Resolve-Path $Path).Path
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments

    $exitCodeVar = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    if (($null -ne $exitCodeVar) -and ($exitCodeVar.Value -ne 0)) {
        throw "Command failed with exit code $($exitCodeVar.Value): $FilePath $($Arguments -join ' ')"
    }
}

function Get-ToolchainDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SdkEnvRoot
    )

    $toolchain = Get-ChildItem (Join-Path $SdkEnvRoot 'toolchains') -Directory |
        Where-Object { $_.Name -like '*-win' } |
        Select-Object -First 1

    if ($null -eq $toolchain) {
        throw "No GNU RISC-V toolchain found under $SdkEnvRoot\toolchains"
    }

    return $toolchain.FullName
}

function Initialize-BuildEnvironment {
    $script:HpmSdkDir = Get-ValidatedPath -Path $SdkDir -Description 'HPM SDK path'
    $script:SdkEnvRoot = Split-Path -Parent $script:HpmSdkDir
    $script:ToolchainDir = Get-ToolchainDir -SdkEnvRoot $script:SdkEnvRoot
    $script:CmakeExe = Get-ValidatedPath -Path (Join-Path $script:SdkEnvRoot 'tools\cmake\bin\cmake.exe') -Description 'CMake executable'
    $script:NinjaExe = Get-ValidatedPath -Path (Join-Path $script:SdkEnvRoot 'tools\ninja\ninja.exe') -Description 'Ninja executable'
    $script:JLinkExe = Get-ValidatedPath -Path (Join-Path $JLinkDir 'JLink.exe') -Description 'JLink executable'
    $script:JLinkGdbServerExe = Get-ValidatedPath -Path (Join-Path $JLinkDir 'JLinkGDBServerCL.exe') -Description 'JLink GDB server executable'

    $env:HPM_SDK_BASE = $script:HpmSdkDir
    $env:GNURISCV_TOOLCHAIN_PATH = $script:ToolchainDir
    $env:HPM_SDK_TOOLCHAIN_VARIANT = 'gcc'
    $env:COMSPEC = 'C:\Windows\System32\cmd.exe'
    $env:PATH = @(
        (Join-Path $script:SdkEnvRoot 'tools\cmake\bin'),
        (Join-Path $script:ToolchainDir 'bin'),
        (Join-Path $script:SdkEnvRoot 'tools\python3'),
        (Join-Path $script:SdkEnvRoot 'tools\python3\Scripts'),
        (Join-Path $script:SdkEnvRoot 'tools\ninja'),
        $JLinkDir,
        'C:\Windows',
        'C:\Windows\System32'
    ) -join ';'
}

function Invoke-Configure {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $cmakeArgs = @(
        '-G', 'Ninja',
        "-DBOARD=$BoardName",
        "-DBOARD_SEARCH_PATH=$BoardSearchPath",
        "-DMAC_TO_MAC_ROLE=$RoleName",
        "-DCMAKE_BUILD_TYPE=$CmakeBuildType",
        "-DHPM_BUILD_TYPE=$HpmBuildType",
        '-S', $ProjectRoot,
        '-B', $BuildDir
    )

    Invoke-External $script:CmakeExe @cmakeArgs
}

function Invoke-Build {
    Invoke-External $script:CmakeExe '--build' $BuildDir
}

function Get-OutputElf {
    $outputDir = Join-Path $BuildDir 'output'
    if (-not (Test-Path $outputDir)) {
        throw "Build output directory not found: $outputDir"
    }

    $elf = Get-ChildItem $outputDir -Filter '*.elf' -File | Select-Object -First 1
    if ($null -eq $elf) {
        throw "No ELF file found under $outputDir"
    }

    return $elf.FullName
}

function Ensure-BuildReady {
    if (-not (Test-Path $BuildDir)) {
        Invoke-Configure
    }

    $outputDir = Join-Path $BuildDir 'output'
    if (-not (Test-Path $outputDir)) {
        Invoke-Build
    }
}

function Invoke-Flash {
    Ensure-BuildReady

    $elfPath = Get-OutputElf
    $jlinkCommandFile = Join-Path $BuildDir 'flash.jlink'
    $jlinkElfPath = $elfPath -replace '\\', '/'
    $jlinkLines = @(
        'JTAGConf -1,-1',
        'r',
        'h',
        "loadfile $jlinkElfPath",
        'r',
        'g',
        'qc'
    )

    Set-Content -Path $jlinkCommandFile -Value $jlinkLines -Encoding ascii

    $jlinkArgs = @(
        '-NoGui', '1',
        '-AutoConnect', '1',
        '-Device', $JLinkDevice,
        '-If', 'JTAG',
        '-Speed', "$JLinkSpeed",
        '-CommandFile', $jlinkCommandFile
    )

    Invoke-External $script:JLinkExe @jlinkArgs
}

function Invoke-GdbServer {
    $gdbServerArgs = @(
        '-if', 'JTAG',
        '-device', $JLinkDevice,
        '-speed', "$JLinkSpeed",
        '-port', '2331',
        '-swoport', '2332',
        '-telnetport', '2333',
        '-RTTTelnetPort', '19021',
        '-singlerun',
        '-nogui'
    )

    Invoke-External $script:JLinkGdbServerExe @gdbServerArgs
}

Initialize-BuildEnvironment

switch ($Action) {
    'configure' {
        Invoke-Configure
    }
    'build' {
        if (-not (Test-Path $BuildDir)) {
            Invoke-Configure
        }
        Invoke-Build
    }
    'rebuild' {
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir
        }
        Invoke-Configure
        Invoke-Build
    }
    'flash' {
        if (-not (Test-Path $BuildDir)) {
            Invoke-Configure
            Invoke-Build
        }
        Invoke-Flash
    }
    'gdbserver' {
        Invoke-GdbServer
    }
    'clean' {
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir
        }
    }
}