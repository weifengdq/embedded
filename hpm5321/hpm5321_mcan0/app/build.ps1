# 构建脚本
# 确保环境变量已设置
$env:HPM_SDK_BASE = "C:\git\embedded\hpm5321\sdk_env_v1.10.0\hpm_sdk"

# 创建构建目录
if (!(Test-Path "build")) {
    mkdir build
}

# 切换到构建目录
Set-Location build

# 运行CMake配置
cmake -G "MinGW Makefiles" -DHPM_BOARD=hpm5321evk ..

# 执行构建
cmake --build .

# 返回到应用目录
Set-Location ..

Write-Host "Build completed. Check build directory for outputs."
