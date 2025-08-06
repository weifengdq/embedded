# HPM5321 项目构建脚本
# 用于环境检查、生成、编译和清理操作

param(
    [Parameter(Position=0)]
    [ValidateSet("check", "configure", "build", "clean", "rebuild", "flash", "reset", "help")]
    [string]$Action = "help",
    
    [Parameter()]
    [string]$SdkPath = "D:/hpm/sdk_env_v1.10.0",
    
    [Parameter()]
    [string]$Board = "hpm4canfd",
    
    [Parameter()]
    [string]$BuildType = "flash_xip",
    
    [Parameter()]
    [string]$ConfigType = "debug"
)

# 配置常量
$script:Config = @{
    # SDK 路径配置
    SDK_BASE_PATH = $SdkPath
    HPM_SDK_PATH = "$SdkPath/hpm_sdk"
    TOOLCHAIN_PATH = "$SdkPath/toolchains/rv32imac_zicsr_zifencei_multilib_b_ext-win"
    
    # 工具路径配置
    CMAKE_EXE = "$SdkPath/tools/cmake/bin/cmake.exe"
    NINJA_EXE = "$SdkPath/tools/ninja/ninja.exe"
    OPENOCD_EXE = "$SdkPath/tools/openocd/openocd.exe"
    PYTHON_EXE = "$SdkPath/tools/python3/python.exe"
    GCC_EXE = "$SdkPath/toolchains/rv32imac_zicsr_zifencei_multilib_b_ext-win/bin/riscv32-unknown-elf-gcc.exe"
    
    # OpenOCD 配置路径
    OPENOCD_SCRIPT_DIR = "$SdkPath/hpm_sdk/boards/openocd"
    
    # 构建配置
    BOARD = $Board
    BUILD_TYPE = $BuildType
    CONFIG_TYPE = $ConfigType
    TOOLCHAIN_VARIANT = "gcc"
    
    # 项目路径
    PROJECT_ROOT = $PSScriptRoot
    BUILD_DIR = Join-Path $PSScriptRoot "build"
    APP_DIR = Join-Path $PSScriptRoot "app"
    OUTPUT_DIR = Join-Path $PSScriptRoot "build/output"
    ELF_FILE = Join-Path $PSScriptRoot "build/output/demo.elf"
    CUSTOM_LINKER = Join-Path $PSScriptRoot "app/linkers/gcc/user_linker.ld"
}

# 颜色输出函数
function Write-ColorText {
    param(
        [string]$Text,
        [ConsoleColor]$Color = [ConsoleColor]::White
    )
    Write-Host $Text -ForegroundColor $Color
}

# 检查环境变量和路径
function Test-Environment {
    Write-ColorText "*************** 环境检查 ***************" -Color Cyan
    
    # 显示当前配置
    Write-ColorText "当前配置:" -Color Yellow
    Write-ColorText "  SDK路径: $($Config.SDK_BASE_PATH)" -Color Gray
    Write-ColorText "  开发板: $($Config.BOARD)" -Color Gray
    Write-ColorText "  构建类型: $($Config.BUILD_TYPE)" -Color Gray
    Write-ColorText "  配置类型: $($Config.CONFIG_TYPE)" -Color Gray
    Write-Host ""
    
    $env_vars = @{
        "HPM_SDK_BASE" = $Config.HPM_SDK_PATH
        "GNURISCV_TOOLCHAIN_PATH" = $Config.TOOLCHAIN_PATH
        "HPM_SDK_TOOLCHAIN_VARIANT" = $Config.TOOLCHAIN_VARIANT
    }
    
    $tools = @{
        "CMAKE" = $Config.CMAKE_EXE
        "NINJA" = $Config.NINJA_EXE
        "GCC" = $Config.GCC_EXE
        "OPENOCD" = $Config.OPENOCD_EXE
        "PYTHON3" = $Config.PYTHON_EXE
    }
    
    $all_ok = $true
    
    # 检查环境变量
    Write-ColorText "检查环境变量..." -Color Yellow
    foreach ($var in $env_vars.GetEnumerator()) {
        $current_value = [Environment]::GetEnvironmentVariable($var.Key)
        if ($current_value -eq $var.Value) {
            Write-ColorText "✓ $($var.Key): $current_value" -Color Green
        } elseif ($current_value) {
            Write-ColorText "⚠ $($var.Key): $current_value (期望: $($var.Value))" -Color Yellow
        } else {
            Write-ColorText "✗ $($var.Key): 未设置 (期望: $($var.Value))" -Color Red
            $all_ok = $false
        }
    }
    
    # 检查工具路径
    Write-ColorText "`n检查工具路径..." -Color Yellow
    foreach ($tool in $tools.GetEnumerator()) {
        if (Test-Path $tool.Value) {
            Write-ColorText "✓ $($tool.Key): $($tool.Value)" -Color Green
        } else {
            Write-ColorText "✗ $($tool.Key): $($tool.Value) (文件不存在)" -Color Red
            $all_ok = $false
        }
    }
    
    # 检查项目结构
    Write-ColorText "`n检查项目结构..." -Color Yellow
    $project_files = @(
        "app/CMakeLists.txt",
        "app/src/main.c",
        "$($Config.BOARD)/board.h",
        "$($Config.BOARD)/$($Config.BOARD).cfg"
    )
    
    foreach ($file in $project_files) {
        $full_path = Join-Path $Config.PROJECT_ROOT $file
        if (Test-Path $full_path) {
            Write-ColorText "✓ $file" -Color Green
        } else {
            Write-ColorText "✗ $file (文件不存在)" -Color Red
            $all_ok = $false
        }
    }
    
    if ($all_ok) {
        Write-ColorText "`n✓ 环境检查通过！" -Color Green
    } else {
        Write-ColorText "`n✗ 环境检查失败，请检查上述问题。" -Color Red
        exit 1
    }
}

# 配置项目
function Invoke-Configure {
    Write-ColorText "*************** 配置项目 ***************" -Color Cyan
    
    # 创建 build 目录
    if (!(Test-Path $Config.BUILD_DIR)) {
        Write-ColorText "创建构建目录: $($Config.BUILD_DIR)" -Color Yellow
        New-Item -ItemType Directory -Path $Config.BUILD_DIR -Force | Out-Null
    }
    
    # 设置环境变量
    $env:HPM_SDK_BASE = $Config.HPM_SDK_PATH
    $env:GNURISCV_TOOLCHAIN_PATH = $Config.TOOLCHAIN_PATH
    $env:HPM_SDK_TOOLCHAIN_VARIANT = $Config.TOOLCHAIN_VARIANT
    
    # CMake 配置命令
    $cmake_args = @(
        "-GNinja"
        "-DCMAKE_MAKE_PROGRAM=$($Config.NINJA_EXE)"
        "-DBOARD=$($Config.BOARD)"
        "-DHPM_BUILD_TYPE=$($Config.BUILD_TYPE)"
        "-DCMAKE_BUILD_TYPE=$($Config.CONFIG_TYPE)"
        "-B", $Config.BUILD_DIR
        "-DBOARD_SEARCH_PATH=$($Config.PROJECT_ROOT)"
        "-DCUSTOM_GCC_LINKER_FILE=$($Config.CUSTOM_LINKER)"
        $Config.APP_DIR
    )
    
    Write-ColorText "执行 CMake 配置..." -Color Yellow
    Write-ColorText "命令: $($Config.CMAKE_EXE) $($cmake_args -join ' ')" -Color Gray
    
    Push-Location $Config.PROJECT_ROOT
    try {
        & $Config.CMAKE_EXE @cmake_args
        if ($LASTEXITCODE -eq 0) {
            Write-ColorText "✓ 配置成功！" -Color Green
        } else {
            Write-ColorText "✗ 配置失败！" -Color Red
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
}

# 编译项目
function Invoke-Build {
    Write-ColorText "*************** 编译项目 ***************" -Color Cyan
    
    if (!(Test-Path $Config.BUILD_DIR)) {
        Write-ColorText "构建目录不存在，先执行配置..." -Color Yellow
        Invoke-Configure
    }
    
    Write-ColorText "执行 Ninja 编译..." -Color Yellow
    Write-ColorText "工作目录: $($Config.BUILD_DIR)" -Color Gray
    
    Push-Location $Config.BUILD_DIR
    try {
        & $Config.NINJA_EXE
        if ($LASTEXITCODE -eq 0) {
            Write-ColorText "✓ 编译成功！" -Color Green
            
            # 显示输出文件信息
            if (Test-Path $Config.OUTPUT_DIR) {
                Write-ColorText "`n生成的文件:" -Color Cyan
                Get-ChildItem $Config.OUTPUT_DIR | ForEach-Object {
                    $size = if ($_.Length -lt 1KB) { "$($_.Length) B" }
                           elseif ($_.Length -lt 1MB) { "{0:N1} KB" -f ($_.Length / 1KB) }
                           else { "{0:N1} MB" -f ($_.Length / 1MB) }
                    Write-ColorText "  $($_.Name) ($size)" -Color White
                }
            }
        } else {
            Write-ColorText "✗ 编译失败！" -Color Red
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
}

# 清理项目
function Invoke-Clean {
    Write-ColorText "*************** 清理项目 ***************" -Color Cyan
    
    if (Test-Path $Config.BUILD_DIR) {
        if (Test-Path (Join-Path $Config.BUILD_DIR "build.ninja")) {
            Write-ColorText "执行 Ninja 清理..." -Color Yellow
            Push-Location $Config.BUILD_DIR
            try {
                & $Config.NINJA_EXE clean
                if ($LASTEXITCODE -eq 0) {
                    Write-ColorText "✓ Ninja 清理完成！" -Color Green
                }
            } finally {
                Pop-Location
            }
        }
        
        # 完全清理选项
        $response = Read-Host "是否完全删除 build 目录? [y/N]"
        if ($response -eq 'y' -or $response -eq 'Y') {
            Write-ColorText "删除构建目录: $($Config.BUILD_DIR)" -Color Yellow
            Remove-Item $Config.BUILD_DIR -Recurse -Force
            Write-ColorText "✓ 完全清理完成！" -Color Green
        }
    } else {
        Write-ColorText "构建目录不存在，无需清理。" -Color Yellow
    }
}

# 重新编译
function Invoke-Rebuild {
    Write-ColorText "*************** 重新编译 ***************" -Color Cyan
    Invoke-Clean
    Invoke-Configure
    Invoke-Build
}

# 烧写固件到 Flash
function Invoke-Flash {
    Write-ColorText "*************** 烧写固件 ***************" -Color Cyan
    
    # 检查 ELF 文件是否存在
    if (!(Test-Path $Config.ELF_FILE)) {
        Write-ColorText "✗ 找不到 demo.elf 文件，请先编译项目。" -Color Red
        Write-ColorText "  路径: $($Config.ELF_FILE)" -Color Gray
        exit 1
    }
    
    # 将 Windows 路径转换为 Unix 风格路径（OpenOCD 需要）
    $elf_file_unix = $Config.ELF_FILE -replace '\\', '/'
    
    $openocd_args = @(
        "-s", $Config.OPENOCD_SCRIPT_DIR
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/probes/ft2232.cfg"
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/soc/hpm5300.cfg"
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/boards/hpm5300evk.cfg"
        "-c", "program `"$elf_file_unix`" verify; reset_soc; exit"
    )
    
    Write-ColorText "准备烧写固件..." -Color Yellow
    Write-ColorText "目标文件: $($Config.ELF_FILE)" -Color Gray
    Write-ColorText "OpenOCD 路径: $elf_file_unix" -Color Gray
    Write-ColorText "文件大小: $((Get-Item $Config.ELF_FILE).Length) 字节" -Color Gray
    Write-ColorText "`n执行 OpenOCD 烧写..." -Color Yellow
    Write-ColorText "命令: $($Config.OPENOCD_EXE) $($openocd_args -join ' ')" -Color Gray
    
    try {
        & $Config.OPENOCD_EXE @openocd_args
        if ($LASTEXITCODE -eq 0) {
            Write-ColorText "✓ 烧写成功！" -Color Green
        } else {
            Write-ColorText "✗ 烧写失败！" -Color Red
            Write-ColorText "请检查：" -Color Yellow
            Write-ColorText "  1. 调试器是否正确连接" -Color White
            Write-ColorText "  2. 目标板是否上电" -Color White
            Write-ColorText "  3. JTAG 连接是否正常" -Color White
            exit $LASTEXITCODE
        }
    } catch {
        Write-ColorText "✗ 执行 OpenOCD 时出错: $($_.Exception.Message)" -Color Red
        exit 1
    }
}

# 复位目标板
function Invoke-Reset {
    Write-ColorText "*************** 复位目标板 ***************" -Color Cyan
    
    $openocd_args = @(
        "-s", $Config.OPENOCD_SCRIPT_DIR
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/probes/ft2232.cfg"
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/soc/hpm5300.cfg"
        "-f", "$($Config.OPENOCD_SCRIPT_DIR)/boards/hpm5300evk.cfg"
        "-c", "init; reset_soc; exit"
    )
    
    Write-ColorText "执行目标板复位..." -Color Yellow
    Write-ColorText "命令: $($Config.OPENOCD_EXE) $($openocd_args -join ' ')" -Color Gray
    
    try {
        & $Config.OPENOCD_EXE @openocd_args
        if ($LASTEXITCODE -eq 0) {
            Write-ColorText "✓ 复位成功！" -Color Green
        } else {
            Write-ColorText "✗ 复位失败！" -Color Red
            Write-ColorText "请检查调试器连接和目标板状态。" -Color Yellow
            exit $LASTEXITCODE
        }
    } catch {
        Write-ColorText "✗ 执行 OpenOCD 时出错: $($_.Exception.Message)" -Color Red
        exit 1
    }
}

# 显示帮助
function Show-Help {
    Write-ColorText "HPM5321 项目构建脚本" -Color Cyan
    Write-Host ""
    Write-ColorText "用法:" -Color Yellow
    Write-Host "  .\win.ps1 <action> [参数]"
    Write-Host ""
    Write-ColorText "可用操作:" -Color Yellow
    Write-Host "  check      - 检查环境和依赖"
    Write-Host "  configure  - 配置项目 (CMake)"
    Write-Host "  build      - 编译项目 (Ninja)"
    Write-Host "  clean      - 清理项目"
    Write-Host "  rebuild    - 重新编译 (清理 + 配置 + 编译)"
    Write-Host "  flash      - 烧写固件到 Flash (OpenOCD)"
    Write-Host "  reset      - 复位目标板 (OpenOCD)"
    Write-Host "  help       - 显示此帮助信息"
    Write-Host ""
    Write-ColorText "可选参数:" -Color Yellow
    Write-Host "  -SdkPath <路径>     - 指定 HPM SDK 路径 (默认: D:/hpm/sdk_env_v1.10.0)"
    Write-Host "  -Board <板型>       - 指定开发板 (默认: hpm4canfd)"
    Write-Host "  -BuildType <类型>   - 指定构建类型 (默认: flash_xip)"
    Write-Host "  -ConfigType <类型>  - 指定配置类型 (默认: debug)"
    Write-Host ""
    Write-ColorText "示例:" -Color Yellow
    Write-Host "  .\win.ps1 check"
    Write-Host "  .\win.ps1 build"
    Write-Host "  .\win.ps1 build -Board hpm5300evk"
    Write-Host "  .\win.ps1 build -SdkPath 'C:/hpm_sdk'"
    Write-Host "  .\win.ps1 flash -ConfigType release"
    Write-Host "  .\win.ps1 reset"
    Write-Host ""
    Write-ColorText "组合使用:" -Color Yellow
    Write-Host "  .\win.ps1 build && .\win.ps1 flash   # 编译并烧写"
    Write-Host ""
    Write-ColorText "迁移到其他项目:" -Color Yellow
    Write-Host "  1. 复制 win.ps1 到新项目根目录"
    Write-Host "  2. 根据需要调整 -Board 参数"
    Write-Host "  3. 确保项目结构符合 HPM SDK 标准"
    Write-Host ""
    Write-ColorText "VS Code 任务:" -Color Yellow
    Write-Host "  也可以使用 Ctrl+Shift+P -> Tasks: Run Task -> HPM: Build"
}

# 主逻辑
Write-ColorText "HPM5321 项目构建脚本" -Color Magenta
Write-ColorText "=============================" -Color Magenta
Write-Host ""

switch ($Action) {
    "check" { Test-Environment }
    "configure" { 
        Test-Environment
        Invoke-Configure 
    }
    "build" { 
        Test-Environment
        Invoke-Build 
    }
    "clean" { Invoke-Clean }
    "rebuild" { 
        Test-Environment
        Invoke-Rebuild 
    }
    "flash" { Invoke-Flash }
    "reset" { Invoke-Reset }
    "help" { Show-Help }
    default { Show-Help }
}
