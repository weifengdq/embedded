[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'clean', 'distclean', 'rebuild', 'gen', 'flash', 'help')]
    [string]$Action = 'build',

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Preset = 'Debug',

    [string]$ProjectName = 'stm32h503_lan9370_lan8720',

    # Optional overrides
    [string]$BuildDir,
    [string]$ElfPath,

    # Flash options
    [string]$CubeProgrammerCli = 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe',
    [string]$STLinkSN,
    [int]$SWDFreqKHz = 4000,
    [ValidateSet('hex', 'bin')]
    [string]$FlashFormat = 'hex',
    [string]$FlashAddress = '0x08000000',

    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

function Write-Info([string]$msg) { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Warn([string]$msg) { Write-Warning $msg }
function Write-Err([string]$msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }

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
    # Matches CMakePresets.json: ${sourceDir}/build/${presetName}
    return (Join-Path -Path $PSScriptRoot -ChildPath (Join-Path 'build' $presetName))
}

function Resolve-ElfFile([string]$buildDir) {
    if ($ElfPath -and (Test-Path $ElfPath)) {
        return $ElfPath
    }
    $elfFile = Join-Path $buildDir "$ProjectName.elf"
    if (Test-Path $elfFile) {
        return $elfFile
    }
    throw "找不到 ELF 文件: $elfFile"
}

function Build-Project {
    Write-Info "构建项目: $ProjectName (配置: $Preset)"
    Require-Command 'cmake'

    $buildDirPath = Resolve-DefaultBuildDir $Preset
    Write-Info "构建目录: $buildDirPath"

    # Generate build files if needed
    if (!(Test-Path (Join-Path $buildDirPath 'CMakeCache.txt'))) {
        Write-Info "生成构建文件..."
        cmake --preset $Preset -B $buildDirPath
        if ($LASTEXITCODE -ne 0) {
            throw "CMake 配置失败"
        }
    }

    # Build
    Write-Info "开始编译..."
    $jobsArg = if ($Jobs -gt 0) { "--parallel $Jobs" } else { "" }
    if ($jobsArg) {
        cmake --build $buildDirPath $jobsArg
    } else {
        cmake --build $buildDirPath
    }
    
    if ($LASTEXITCODE -ne 0) {
        throw "编译失败"
    }

    $elfFile = Resolve-ElfFile $buildDirPath
    Write-Info "编译成功: $elfFile"
    
    # Display size
    $armSize = "arm-none-eabi-size"
    if (Get-Command $armSize -ErrorAction SilentlyContinue) {
        Write-Info "固件大小:"
        & $armSize $elfFile
    }
}

function Clean-Project {
    Write-Info "清理构建文件..."
    $buildDirPath = Resolve-DefaultBuildDir $Preset
    if (Test-Path $buildDirPath) {
        cmake --build $buildDirPath --target clean
        Write-Info "清理完成"
    } else {
        Write-Warn "构建目录不存在: $buildDirPath"
    }
}

function DistClean-Project {
    Write-Info "删除构建目录..."
    $buildDirPath = Resolve-DefaultBuildDir $Preset
    if (Test-Path $buildDirPath) {
        Remove-Item -Path $buildDirPath -Recurse -Force
        Write-Info "构建目录已删除: $buildDirPath"
    } else {
        Write-Warn "构建目录不存在: $buildDirPath"
    }
}

function Rebuild-Project {
    Clean-Project
    Build-Project
}

function Gen-BuildFiles {
    Write-Info "生成构建文件..."
    Require-Command 'cmake'
    
    $buildDirPath = Resolve-DefaultBuildDir $Preset
    Write-Info "构建目录: $buildDirPath"
    
    cmake --preset $Preset -B $buildDirPath
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败"
    }
    Write-Info "构建文件生成完成"
}

function Flash-Firmware {
    Write-Info "烧录固件到 STM32..."
    
    # Check STM32CubeProgrammer
    if (!(Test-Path $CubeProgrammerCli)) {
        throw "找不到 STM32CubeProgrammer CLI: $CubeProgrammerCli"
    }
    
    $buildDirPath = Resolve-DefaultBuildDir $Preset
    $elfFile = Resolve-ElfFile $buildDirPath
    Require-Command 'arm-none-eabi-objcopy'
    
    # Convert to hex or bin if needed
    $flashFile = $elfFile
    if ($FlashFormat -eq 'hex') {
        $hexFile = [IO.Path]::ChangeExtension($elfFile, '.hex')
        $needConvert = $true
        if (Test-Path $hexFile) {
            $needConvert = (Get-Item $hexFile).LastWriteTime -lt (Get-Item $elfFile).LastWriteTime
        }
        if ($needConvert) {
            Write-Info "转换为 HEX 格式..."
            & arm-none-eabi-objcopy -O ihex $elfFile $hexFile
            if ($LASTEXITCODE -ne 0) {
                throw "HEX 转换失败"
            }
        }
        $flashFile = $hexFile
    } elseif ($FlashFormat -eq 'bin') {
        $binFile = [IO.Path]::ChangeExtension($elfFile, '.bin')
        $needConvert = $true
        if (Test-Path $binFile) {
            $needConvert = (Get-Item $binFile).LastWriteTime -lt (Get-Item $elfFile).LastWriteTime
        }
        if ($needConvert) {
            Write-Info "转换为 BIN 格式..."
            & arm-none-eabi-objcopy -O binary $elfFile $binFile
            if ($LASTEXITCODE -ne 0) {
                throw "BIN 转换失败"
            }
        }
        $flashFile = $binFile
    }
    
    Write-Info "烧录文件: $flashFile"
    
    # Build programmer command
    $progParams = @(
        '-c', 'port=SWD'
        'freq=' + $SWDFreqKHz
    )
    
    if ($STLinkSN) {
        $progParams += "sn=$STLinkSN"
    }
    
    $progParams += @(
        '-w', $flashFile, $FlashAddress
        '-v'
        '-rst'
    )
    
    & $CubeProgrammerCli $progParams
    
    if ($LASTEXITCODE -eq 0) {
        Write-Info "烧录成功!"
    } else {
        throw "烧录失败"
    }
}

function Show-Help {
    Write-Host @"
LAN9370 STM32H503 构建脚本

用法: .\build.ps1 [action] [preset] [options]

Actions:
  build      - 编译项目 (默认)
  clean      - 清理构建产物
  distclean  - 删除构建目录
  rebuild    - 清理后重新编译
  gen        - 生成构建文件
  flash      - 烧录固件到 STM32
  help       - 显示此帮助信息

Presets:
  Debug      - 调试版本 (默认)
  Release    - 发布版本

选项:
  -ProjectName <name>           项目名称 (默认: stm32h503_lan9370)
  -BuildDir <path>              自定义构建目录
  -ElfPath <path>               自定义 ELF 文件路径
  -CubeProgrammerCli <path>     STM32CubeProgrammer CLI 路径
  -STLinkSN <sn>                STLink 序列号
  -SWDFreqKHz <freq>            SWD 频率 (kHz, 默认: 4000)
  -FlashFormat <hex|bin>        烧录格式 (默认: hex)
  -FlashAddress <addr>          烧录地址 (默认: 0x08000000)
  -Jobs <n>                     并行编译任务数 (0=自动)

示例:
  .\build.ps1                    # 编译调试版本
  .\build.ps1 build Release      # 编译发布版本
  .\build.ps1 clean              # 清理
  .\build.ps1 flash              # 烧录固件
  .\build.ps1 rebuild Debug -Jobs 8   # 8线程重新编译

"@
}

# Main logic
try {
    switch ($Action) {
        'build'     { Build-Project }
        'clean'     { Clean-Project }
        'distclean' { DistClean-Project }
        'rebuild'   { Rebuild-Project }
        'gen'       { Gen-BuildFiles }
        'flash'     { Flash-Firmware }
        'help'      { Show-Help }
        default     { throw "未知操作: $Action" }
    }
    
    Write-Host "`n操作完成!" -ForegroundColor Green
    exit 0
}
catch {
    Write-Err $_.Exception.Message
    Write-Host "`n操作失败!" -ForegroundColor Red
    exit 1
}
