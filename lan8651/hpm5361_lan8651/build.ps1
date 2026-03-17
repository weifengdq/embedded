param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('configure', 'build', 'rebuild', 'flash', 'clean')]
    [string]$Action,

    [Parameter(Mandatory = $false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkBase = Resolve-Path (Join-Path $ProjectRoot '..\..\..')
$HpmSdkDir = Join-Path $SdkBase 'hpm_sdk'
$CmakeExe = Join-Path $SdkBase 'tools\cmake\bin\cmake.exe'
$NinjaExe = Join-Path $SdkBase 'tools\ninja\ninja.exe'
$OpenocdExe = Join-Path $SdkBase 'tools\openocd\openocd.exe'
$CmakeBuildType = if ($Config -eq 'Release') { 'release' } else { 'debug' }
$BuildDirSuffix = if ($Config -eq 'Release') { 'release' } else { 'debug' }
$BuildDir = Join-Path $ProjectRoot ("hpm5300evk_flash_xip_{0}" -f $BuildDirSuffix)
$ToolchainDir = (Get-ChildItem (Join-Path $SdkBase 'toolchains') -Directory | Where-Object { $_.Name -like '*-win' } | Select-Object -First 1).FullName

$env:HPM_SDK_BASE = $HpmSdkDir
$env:GNURISCV_TOOLCHAIN_PATH = $ToolchainDir
$env:HPM_SDK_TOOLCHAIN_VARIANT = 'gcc'
$env:COMSPEC = 'C:\Windows\System32\cmd.exe'
$env:PATH = @(
    (Join-Path $SdkBase 'tools\cmake\bin'),
    (Join-Path $ToolchainDir 'bin'),
    (Join-Path $SdkBase 'tools\python3'),
    (Join-Path $SdkBase 'tools\python3\Scripts'),
    (Join-Path $SdkBase 'tools\ninja'),
    (Join-Path $SdkBase 'tools\openocd'),
    (Join-Path $SdkBase 'tools\scripts'),
    'C:\Windows',
    'C:\Windows\System32'
) -join ';'

function Invoke-Configure {
    $cmakeArgs = @(
        '-G', 'Ninja',
        '-DBOARD=hpm5300evk',
        "-DCMAKE_BUILD_TYPE=$CmakeBuildType",
        '-DHPM_BUILD_TYPE=flash_xip',
        '-S', $ProjectRoot,
        '-B', $BuildDir
    )
    & $CmakeExe @cmakeArgs
}

function Invoke-Build {
    & $CmakeExe --build $BuildDir
}

function Invoke-Flash {
    $ElfPath = Join-Path $BuildDir 'output\demo.elf'
    if (-not (Test-Path $ElfPath)) {
        throw "ELF not found: $ElfPath"
    }

    $OpenocdSdkBase = ($HpmSdkDir -replace '\\', '/')
    $OpenocdElfPath = ($ElfPath -replace '\\', '/')

    & $OpenocdExe `
        -c "set HPM_SDK_BASE $OpenocdSdkBase; set BOARD hpm5300evk; set PROBE ft2232;" `
        -f (Join-Path $HpmSdkDir 'boards\openocd\hpm5300_all_in_one.cfg') `
        -c "program $OpenocdElfPath verify reset exit"
}

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
    'clean' {
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir
        }
    }
}