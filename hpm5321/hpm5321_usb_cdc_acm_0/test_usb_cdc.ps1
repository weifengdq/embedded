#!/usr/bin/env powershell

# HPM5321 USB CDC ACM 测试脚本

Write-Host "=== HPM5321 USB CDC ACM Echo Test Build Script ===" -ForegroundColor Green

# 检查当前目录
$currentDir = Get-Location
Write-Host "Current directory: $currentDir" -ForegroundColor Yellow

# 检查是否在正确的目录
if (-not (Test-Path "app\CMakeLists.txt")) {
    Write-Host "Error: CMakeLists.txt not found. Please run this script from the project root directory." -ForegroundColor Red
    exit 1
}

# 检查SDK环境
if (-not $env:HPM_SDK_BASE) {
    Write-Host "Error: HPM_SDK_BASE environment variable not set." -ForegroundColor Red
    Write-Host "Please set HPM_SDK_BASE to your HPM SDK installation path." -ForegroundColor Yellow
    exit 1
}

Write-Host "HPM SDK Base: $env:HPM_SDK_BASE" -ForegroundColor Green

# 构建项目
Write-Host "Building USB CDC ACM project..." -ForegroundColor Yellow

try {
    # 运行构建脚本
    .\win.ps1 -a build
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Build completed successfully!" -ForegroundColor Green
        Write-Host ""
        Write-Host "=== Next Steps ===" -ForegroundColor Cyan
        Write-Host "1. Connect HPM5321 board to PC via USB"
        Write-Host "2. Flash the firmware: .\win.ps1 -a flash"
        Write-Host "3. Open serial monitor to see debug output (115200 baud)"
        Write-Host "4. Check Device Manager for new COM port"
        Write-Host "5. Connect to virtual COM port and test echo functionality"
        Write-Host ""
        Write-Host "=== Test Echo ===" -ForegroundColor Cyan
        Write-Host "- Send any text to the virtual COM port"
        Write-Host "- You should receive the same text back (echo)"
        Write-Host "- LED should flash to indicate system is running"
    } else {
        Write-Host "Build failed with exit code: $LASTEXITCODE" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host "Build failed with error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
