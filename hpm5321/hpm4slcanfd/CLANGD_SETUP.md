# HPM4SLCANFD 项目的 clangd 配置

本项目已经为 clangd 语言服务器进行了完整配置，以提供最佳的 C 语言开发体验。

## 配置文件说明

### `.clangd`
主要的 clangd 配置文件，包含：
- RISC-V 目标架构设置 (`riscv32-unknown-elf`)
- HPM5321 的架构标志 (`rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs`)
- 适合嵌入式开发的警告配置
- 语义高亮和诊断设置
- clang-tidy 检查规则

### `.vscode/settings.json`
VS Code 特定设置：
- 启用 clangd 扩展并禁用 C++ 扩展冲突
- 配置编译命令数据库路径
- 开启语义高亮和代码格式化
- 设置回退编译标志

### `.vscode/c_cpp_properties.json`
C++ 扩展配置（已禁用 IntelliSense）：
- HPM SDK 包含路径
- 预定义宏
- 编译器路径

### `.clang-format`
代码格式化配置：
- 基于 LLVM 样式
- 4 空格缩进
- Linux 风格花括号
- 包含文件排序规则

## 功能特性

✅ **智能代码补全**: 基于实际编译命令的精确补全
✅ **实时错误检测**: 编译时错误和警告的即时反馈
✅ **代码导航**: 跳转到定义、查找引用、符号搜索
✅ **语义高亮**: 基于语义的代码高亮显示
✅ **代码格式化**: 一致的代码风格格式化
✅ **重构支持**: 重命名、提取函数等重构操作
✅ **clang-tidy 集成**: 静态代码分析和建议

## 使用说明

### 首次设置
1. 确保安装了 clangd 扩展
2. 禁用 C/C++ 扩展（如果已安装）
3. 运行构建任务生成 `compile_commands.json`

### 构建任务
- **HPM: Configure**: 配置 CMake 构建
- **HPM: Build**: 编译项目
- **HPM: Clean**: 清理构建文件
- **HPM: Rebuild**: 重新构建
- **HPM: Refresh clangd**: 刷新 clangd 编译数据库

### 验证配置
```bash
cd c:\git\embedded\hpm5321\hpm4slcanfd
clangd --check=app/src/main.c
```

应该显示 "All checks completed, 0 errors"

## 故障排除

### 常见问题
1. **找不到头文件**: 确保运行了构建配置任务
2. **编译错误**: 检查 SDK 路径是否正确
3. **IntelliSense 冲突**: 确保禁用了 C++ 扩展

### 重新生成编译数据库
```bash
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

## 文件结构
```
hpm4slcanfd/
├── .clangd                 # clangd 主配置
├── .clang-format          # 代码格式化配置
├── .vscode/
│   ├── settings.json      # VS Code 设置
│   ├── c_cpp_properties.json
│   ├── tasks.json         # 构建任务
│   └── extensions.json    # 推荐扩展
├── build/
│   └── compile_commands.json  # 编译命令数据库
├── app/
│   └── src/main.c         # 主要源文件
└── hpm4canfd/            # 板级支持包
```

配置完成！现在你可以享受现代的 C 语言开发体验了。
