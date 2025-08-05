# HPM5321 工程生成器

基于 `hpm5321_led` 模板快速创建新的 HPM5321 工程的 PowerShell 脚本。

## 功能特性

- 🚀 基于现有模板快速创建新工程
- 📝 自动更新项目名称和板子配置
- 🛡️ 智能排除构建临时文件
- 🎯 支持多种开发板类型
- 💪 支持强制覆盖现有工程

## 使用方法

### 基本用法

```powershell
# 创建新工程（使用默认板子 hpm4canfd）
.\generate_new_pj.ps1 hpm5321_uart

# 指定板子类型创建工程
.\generate_new_pj.ps1 hpm5321_can hpm5300evk

# 强制覆盖已存在的工程
.\generate_new_pj.ps1 hpm5321_spi hpm6750evk -Force
```

### 参数说明

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `ProjectName` | string | ✅ | 新工程的名称 |
| `BoardName` | string | ❌ | 目标板子名称（默认: hpm4canfd） |
| `-Force` | switch | ❌ | 强制覆盖已存在的工程目录 |

### 支持的板子类型

- `hpm4canfd` - HPM4CANFD 开发板（默认）
- `hpm5300evk` - HPM5300EVK 开发板
- `hpm6750evk` - HPM6750EVK 开发板
- `hpm6360evk` - HPM6360EVK 开发板

## 脚本功能

### 文件复制
脚本会复制模板工程的以下内容：
- 源代码文件（`app/src/`）
- CMake 配置文件
- 链接器脚本（`app/linkers/`）
- VS Code 配置（`.vscode/`）
- 板子相关配置（如 `hpm4canfd/`）
- 构建脚本和配置文件

### 智能排除
以下文件和目录会被自动排除：
- `build/` - 构建输出目录
- `.cache/` - 缓存目录
- `.vector/` - Vector 相关文件
- `*.hex`, `*.bin`, `*.elf`, `*.map` - 构建产物
- `build.config.ps1` - 个人构建配置（会重新生成）

### 自动更新
脚本会自动更新以下内容：
- **项目名称**: 在 CMakeLists.txt 中更新 `project()` 名称
- **板子配置**: 在构建配置文件中更新板子类型
- **文件内容**: 递归搜索并替换所有相关文件中的项目名称

## 使用示例

### 创建 UART 工程
```powershell
.\generate_new_pj.ps1 hpm5321_uart
```

生成的工程结构：
```
hpm5321_uart/
├── app/
│   ├── src/
│   │   └── main.c
│   ├── linkers/
│   └── CMakeLists.txt
├── .vscode/
├── hpm4canfd/
├── build.config.ps1
├── build.config.example.ps1
├── win.ps1
└── README_BUILD.md
```

### 创建 CAN 工程（使用 HPM5300EVK）
```powershell
.\generate_new_pj.ps1 hpm5321_can hpm5300evk
```

### 强制覆盖现有工程
```powershell
.\generate_new_pj.ps1 hpm5321_spi hpm6750evk -Force
```

## 后续步骤

工程创建成功后，按以下步骤进行开发：

1. **进入工程目录**
   ```powershell
   cd hpm5321_uart
   ```

2. **检查和修改构建配置**（可选）
   ```powershell
   # 编辑构建配置文件
   notepad build.config.ps1
   ```

3. **检查开发环境**
   ```powershell
   .\win.ps1 check
   ```

4. **配置工程**
   ```powershell
   .\win.ps1 configure
   ```

5. **编译工程**
   ```powershell
   .\win.ps1 build
   ```

6. **烧录程序**
   ```powershell
   .\win.ps1 flash
   ```

## 故障排除

### 常见错误

1. **模板目录不存在**
   ```
   错误: 模板目录不存在: hpm5321_led
   ```
   确保 `hpm5321_led` 目录存在于脚本同级目录。

2. **目标工程已存在**
   ```
   错误: 目标工程已存在: hpm5321_uart
   使用 -Force 参数强制覆盖
   ```
   使用 `-Force` 参数或手动删除现有目录。

3. **权限不足**
   确保在管理员权限下运行 PowerShell，或者目标目录有写入权限。

### 获取帮助

```powershell
.\generate_new_pj.ps1 help
```

## 注意事项

- 🚨 使用 `-Force` 参数会完全删除现有的同名工程目录
- 📁 脚本会保持原有的目录结构和文件权限
- 🔧 生成的 `build.config.ps1` 需要根据实际 SDK 路径进行调整
- 📝 建议在创建工程后立即修改 `main.c` 中的具体业务逻辑

## 版本信息

- 创建日期: 2025年8月5日
- 适用版本: HPM SDK v1.10.0+
- 兼容系统: Windows 10/11 (PowerShell 5.1+)
