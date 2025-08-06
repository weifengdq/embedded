# HPM5321 新工程生成脚本
# 基于 hpm5321_led 模板创建新工程

param(
    [Parameter(Mandatory=$false, Position=0)]
    [string]$ProjectName,
    
    [Parameter(Position=1)]
    [string]$BoardName = "hpm4canfd",
    
    [Parameter()]
    [string]$TemplatePath = "hpm5321_led",
    
    [Parameter()]
    [switch]$Force,

    [Parameter()]
    [Alias("h")]
    [switch]$Help
)

# 脚本配置
$script:Config = @{
    TemplateDir = Join-Path $PSScriptRoot $TemplatePath
    TargetDir = Join-Path $PSScriptRoot $ProjectName
    ExcludePatterns = @(
        "build",
        ".cache", 
        ".vector",
        "*.hex",
        "*.bin", 
        "*.elf",
        "*.map",
        "build.config.ps1"
    )
}

# 颜色输出函数
function Write-ColorText {
    param(
        [string]$Text,
        [ConsoleColor]$Color = [ConsoleColor]::White
    )
    $oldColor = $Host.UI.RawUI.ForegroundColor
    $Host.UI.RawUI.ForegroundColor = $Color
    Write-Host $Text
    $Host.UI.RawUI.ForegroundColor = $oldColor
}

# 显示帮助信息
function Show-Help {
    Write-ColorText "HPM5321 新工程生成脚本" -Color Green
    Write-Host ""
    Write-Host "用法:"
    Write-Host "  .\generate_new_pj.ps1 <工程名> [板子名] [-Force] [-Help|-h]"
    Write-Host ""
    Write-Host "参数:"
    Write-Host "  工程名    : 新工程的名称（必填，除非使用 -Help）"
    Write-Host "  板子名    : 目标板子名称（默认: hpm4canfd）"
    Write-Host "  -Force    : 强制覆盖已存在的工程目录"
    Write-Host "  -Help|-h  : 显示此帮助信息"
    Write-Host ""
    Write-Host "示例:"
    Write-Host "  .\generate_new_pj.ps1 -h"
    Write-Host "  .\generate_new_pj.ps1 -Help"
    Write-Host "  .\generate_new_pj.ps1 hpm5321_uart"
    Write-Host "  .\generate_new_pj.ps1 hpm5321_can hpm5300evk"
    Write-Host "  .\generate_new_pj.ps1 hpm5321_spi hpm6750evk -Force"
    Write-Host ""
    Write-Host "支持的板子类型:"
    Write-Host "  - hpm4canfd"
    Write-Host "  - hpm5300evk" 
    Write-Host "  - hpm6750evk"
    Write-Host "  - hpm6360evk"
}

# 检查是否需要排除文件/目录
function Test-ShouldExclude {
    param([string]$Path)
    
    $fileName = Split-Path $Path -Leaf
    foreach ($pattern in $script:Config.ExcludePatterns) {
        if ($fileName -like $pattern) {
            return $true
        }
    }
    return $false
}

# 递归复制文件，排除指定的文件和目录
function Copy-ProjectFiles {
    param(
        [string]$SourceDir,
        [string]$TargetDir,
        [string]$RelativePath = ""
    )
    
    if (-not (Test-Path $SourceDir)) {
        throw "源目录不存在: $SourceDir"
    }
    
    # 创建目标目录
    if (-not (Test-Path $TargetDir)) {
        New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    }
    
    # 获取源目录中的所有项目
    $items = Get-ChildItem $SourceDir
    
    foreach ($item in $items) {
        $currentRelativePath = if ($RelativePath) { "$RelativePath\$($item.Name)" } else { $item.Name }
        
        # 检查是否应该排除
        if (Test-ShouldExclude $item.FullName) {
            Write-ColorText "  跳过: $currentRelativePath" -Color Yellow
            continue
        }
        
        $targetPath = Join-Path $TargetDir $item.Name
        
        if ($item.PSIsContainer) {
            # 递归处理目录
            Write-ColorText "  目录: $currentRelativePath" -Color Cyan
            Copy-ProjectFiles $item.FullName $targetPath $currentRelativePath
        } else {
            # 复制文件
            Write-ColorText "  文件: $currentRelativePath" -Color Gray
            Copy-Item $item.FullName $targetPath -Force
        }
    }
}

# 替换文件内容中的项目名
function Update-FileContent {
    param(
        [string]$FilePath,
        [string]$OldProjectName,
        [string]$NewProjectName,
        [string]$NewBoardName
    )
    
    if (-not (Test-Path $FilePath)) {
        return
    }
    
    try {
        $content = Get-Content $FilePath -Raw -Encoding UTF8
        $modified = $false
        
        # 替换项目名
        if ($content -match $OldProjectName) {
            $content = $content -replace $OldProjectName, $NewProjectName
            $modified = $true
        }
        
        # 替换板子名（仅在特定文件中）
        $fileName = Split-Path $FilePath -Leaf
        if ($fileName -in @("build.config.example.ps1", "win.ps1")) {
            if ($content -match 'Board\s*=\s*"[^"]*"') {
                $content = $content -replace 'Board\s*=\s*"[^"]*"', "Board = `"$NewBoardName`""
                $modified = $true
            }
            if ($content -match '\$Board\s*=\s*"[^"]*"') {
                $content = $content -replace '\$Board\s*=\s*"[^"]*"', "`$Board = `"$NewBoardName`""
                $modified = $true
            }
        }
        
        if ($modified) {
            $content | Set-Content $FilePath -Encoding UTF8 -NoNewline
            Write-ColorText "    更新: $(Split-Path $FilePath -Leaf)" -Color Green
        }
    }
    catch {
        Write-ColorText "    警告: 无法更新文件 $FilePath - $($_.Exception.Message)" -Color Red
    }
}

# 递归更新所有文件内容
function Update-AllFiles {
    param(
        [string]$TargetDir,
        [string]$OldProjectName,
        [string]$NewProjectName,
        [string]$NewBoardName
    )
    
    Write-ColorText "正在更新文件内容..." -Color Blue
    
    # 需要更新的文件扩展名
    $extensions = @("*.txt", "*.c", "*.h", "*.cpp", "*.hpp", "*.ps1", "*.json", "*.md", "*.cmake")
    
    foreach ($ext in $extensions) {
        $files = Get-ChildItem $TargetDir -Filter $ext -Recurse
        foreach ($file in $files) {
            Update-FileContent $file.FullName $OldProjectName $NewProjectName $NewBoardName
        }
    }
}

# 创建构建配置文件
function New-BuildConfig {
    param(
        [string]$TargetDir,
        [string]$BoardName
    )
    
    $configPath = Join-Path $TargetDir "build.config.ps1"
    $examplePath = Join-Path $TargetDir "build.config.example.ps1"
    
    if (Test-Path $examplePath) {
        Copy-Item $examplePath $configPath
        
        # 更新板子配置
        $content = Get-Content $configPath -Raw
        $content = $content -replace 'Board\s*=\s*"[^"]*"', "Board = `"$BoardName`""
        $content | Set-Content $configPath -Encoding UTF8 -NoNewline
        
        Write-ColorText "  创建构建配置: build.config.ps1" -Color Green
    }
}

# 主函数
function Main {
    # 显示帮助
    if ($Help -or (-not $ProjectName) -or $ProjectName -eq "help" -or $ProjectName -eq "-h") {
        Show-Help
        return
    }
    
    Write-ColorText "HPM5321 新工程生成器" -Color Green
    Write-ColorText "========================" -Color Green
    
    # 验证模板目录
    if (-not (Test-Path $script:Config.TemplateDir)) {
        Write-ColorText "错误: 模板目录不存在: $($script:Config.TemplateDir)" -Color Red
        return 1
    }
    
    # 检查目标目录
    if (Test-Path $script:Config.TargetDir) {
        if (-not $Force) {
            Write-ColorText "错误: 目标工程已存在: $ProjectName" -Color Red
            Write-ColorText "使用 -Force 参数强制覆盖" -Color Yellow
            return 1
        } else {
            Write-ColorText "警告: 将覆盖现有工程: $ProjectName" -Color Yellow
            Remove-Item $script:Config.TargetDir -Recurse -Force
        }
    }
    
    try {
        # 显示配置信息
        Write-Host ""
        Write-ColorText "配置信息:" -Color Blue
        Write-Host "  模板工程: $TemplatePath"
        Write-Host "  新工程名: $ProjectName"
        Write-Host "  目标板子: $BoardName"
        Write-Host "  目标路径: $($script:Config.TargetDir)"
        Write-Host ""
        
        # 复制文件
        Write-ColorText "正在复制项目文件..." -Color Blue
        Copy-ProjectFiles $script:Config.TemplateDir $script:Config.TargetDir
        
        # 更新文件内容
        Update-AllFiles $script:Config.TargetDir $TemplatePath $ProjectName $BoardName
        
        # 创建构建配置
        Write-ColorText "正在生成构建配置..." -Color Blue
        New-BuildConfig $script:Config.TargetDir $BoardName
        
        Write-Host ""
        Write-ColorText "✓ 新工程创建成功!" -Color Green
        Write-Host ""
        Write-ColorText "后续步骤:" -Color Blue
        Write-Host "1. cd $ProjectName"
        Write-Host "2. 修改 build.config.ps1 配置文件（如果需要）"
        Write-Host "3. .\win.ps1 check     # 检查环境"
        Write-Host "4. .\win.ps1 configure # 配置工程"
        Write-Host "5. .\win.ps1 build     # 编译工程"
        
        return 0
    }
    catch {
        Write-ColorText "错误: $($_.Exception.Message)" -Color Red
        return 1
    }
}

# 执行主函数
$exitCode = Main
exit $exitCode
