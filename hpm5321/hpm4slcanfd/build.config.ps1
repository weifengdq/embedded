# HPM SDK 构建配置文件示例
# 复制此文件为 build.config.ps1 并根据需要修改

# HPM SDK 路径配置
$BuildConfig = @{
    # SDK 根路径 - 根据你的安装路径修改
    SdkPath = "D:/hpm/sdk_env_v1.10.0"
    
    # 开发板类型 - 根据你的硬件修改
    # 常用选项: hpm4canfd, hpm5300evk, hpm6750evk, hpm6360evk
    Board = "hpm4canfd"
    
    # 构建类型 - 根据需要修改
    # 选项: flash_xip, flash_xip_nor, ram, secboot_only
    BuildType = "flash_xip"
    
    # 配置类型 - 根据需要修改
    # 选项: debug, release
    ConfigType = "release"
}

# 高级配置（可选）
$AdvancedConfig = @{
    # 自定义工具链路径（如果不使用默认路径）
    # ToolchainPath = "C:/custom/toolchain/path"
    
    # 自定义 OpenOCD 配置文件（如果有特殊需求）
    # OpenOcdProbe = "jlink.cfg"      # 默认: ft2232.cfg
    # OpenOcdSoc = "hpm6750.cfg"      # 默认: hpm5300.cfg
    # OpenOcdBoard = "hpm4canfd" # 默认: hpm5300evk.cfg
    
    # 额外的 CMake 参数
    # ExtraCMakeArgs = @("-DENABLE_FEATURE=ON", "-DDEBUG_LEVEL=2")
}

# 项目特定配置
$ProjectConfig = @{
    # 自定义链接器脚本路径（相对于项目根目录）
    CustomLinkerScript = "app/linkers/gcc/user_linker.ld"
    
    # 项目名称（用于生成的文件名）
    ProjectName = "hpm4canfd"
    
    # 输出文件扩展名
    OutputExtension = "elf"
}

# 导出配置供 win.ps1 使用
return @{
    BuildConfig = $BuildConfig
    AdvancedConfig = $AdvancedConfig
    ProjectConfig = $ProjectConfig
}
