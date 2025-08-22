# HPM SDK 构建脚本使用指南

这是一个通用的 HPM SDK 项目构建脚本，支持环境检查、配置、编译、烧写等功能。

## 🚀 主要特性

- **参数化配置**：支持通过命令行参数自定义 SDK 路径、开发板类型等
- **路径抽象**：所有路径都抽象为可配置参数，易于维护
- **项目迁移友好**：可轻松迁移到其他 HPM SDK 项目
- **完整工作流**：从环境检查到固件烧写的完整开发流程
- **错误诊断**：详细的错误信息和故障排除建议

## 📖 基本用法

### 命令格式
```powershell
.\win.ps1 <操作> [参数]
```

### 可用操作
- `check` - 检查环境和依赖
- `configure` - 配置项目 (CMake)
- `build` - 编译项目 (Ninja)
- `clean` - 清理项目
- `rebuild` - 重新编译 (清理 + 配置 + 编译)
- `flash` - 烧写固件到 Flash (OpenOCD)
- `reset` - 复位目标板 (OpenOCD)
- `help` - 显示帮助信息

### 可选参数
- `-SdkPath <路径>` - 指定 HPM SDK 路径 (默认: D:/hpm/sdk_env_v1.10.0)
- `-Board <板型>` - 指定开发板 (默认: hpm4canfd)
- `-BuildType <类型>` - 指定构建类型 (默认: flash_xip)
- `-ConfigType <类型>` - 指定配置类型 (默认: debug)

## 💡 使用示例

### 基本操作
```powershell
# 检查环境
.\win.ps1 check

# 编译项目
.\win.ps1 build

# 烧写固件
.\win.ps1 flash

# 复位目标板
.\win.ps1 reset
```

### 使用自定义参数
```powershell
# 使用不同的开发板
.\win.ps1 build -Board hpm5300evk

# 使用自定义 SDK 路径
.\win.ps1 build -SdkPath "C:/my_hpm_sdk"

# 编译 release 版本
.\win.ps1 build -ConfigType release

# 组合多个参数
.\win.ps1 build -Board hpm6750evk -ConfigType release -SdkPath "D:/hpm_v2.0"
```

### 完整工作流
```powershell
# 1. 检查环境
.\win.ps1 check

# 2. 清理并重新编译
.\win.ps1 rebuild

# 3. 烧写固件
.\win.ps1 flash

# 或者一步到位
.\win.ps1 build && .\win.ps1 flash
```

## 🔧 项目迁移

要将此脚本迁移到其他 HPM SDK 项目：

1. **复制脚本**：将 `win.ps1` 复制到新项目根目录

2. **检查项目结构**：确保项目结构符合 HPM SDK 标准：
   ```
   项目根目录/
   ├── app/
   │   ├── CMakeLists.txt
   │   ├── src/main.c
   │   └── linkers/gcc/user_linker.ld
   ├── <board_name>/
   │   ├── board.h
   │   └── <board_name>.cfg
   └── win.ps1
   ```

3. **调整参数**：根据新项目需要调整默认参数：
   ```powershell
   # 示例：迁移到 HPM6750EVK 项目
   .\win.ps1 build -Board hpm6750evk
   ```

## 🛠️ 支持的开发板

常用的开发板类型：
- `hpm4canfd` - HPM4CANFD 开发板
- `hpm5300evk` - HPM5300EVK 开发板
- `hpm6750evk` - HPM6750EVK 开发板
- `hpm6360evk` - HPM6360EVK 开发板

## 📋 构建类型说明

### BuildType（构建类型）
- `flash_xip` - 在 Flash 中执行（默认）
- `flash_xip_nor` - 在 NOR Flash 中执行
- `ram` - 在 RAM 中执行
- `secboot_only` - 仅安全启动

### ConfigType（配置类型）
- `debug` - 调试版本（默认）
- `release` - 发布版本

## 🔍 故障排除

### 常见问题

1. **SDK 路径错误**
   ```
   ✗ CMAKE: D:/hpm/sdk_env_v1.10.0/tools/cmake/bin/cmake.exe (文件不存在)
   ```
   **解决方案**：使用 `-SdkPath` 参数指定正确的 SDK 路径

2. **开发板文件不存在**
   ```
   ✗ hpm5300evk/board.h (文件不存在)
   ```
   **解决方案**：确认开发板类型，或者在项目中添加对应的开发板文件

3. **烧写失败**
   ```
   ✗ 烧写失败！
   ```
   **解决方案**：检查调试器连接、目标板上电状态、JTAG 连接

### 环境检查
使用 `check` 命令可以快速诊断环境问题：
```powershell
.\win.ps1 check
```

## 🎯 高级用法

### 自定义配置文件
可以创建 `build.config.ps1` 文件来预设常用配置：
```powershell
# 复制示例配置文件
cp build.config.example.ps1 build.config.ps1
# 然后编辑 build.config.ps1 设置你的默认值
```

### VS Code 集成
在 VS Code 中可以使用快捷键：
- `Ctrl+Shift+B` - 快速构建
- `Ctrl+Shift+P` → "Tasks: Run Task" → 选择相应任务

## 📞 支持

如遇问题，请：
1. 使用 `.\win.ps1 check` 检查环境
2. 查看错误信息和建议
3. 确认参数设置是否正确
4. 检查项目结构是否符合要求
