param(
    [Parameter(Mandatory=$true)]
    [string]$SourceFolder,
    
    [Parameter(Mandatory=$true)]
    [string]$TargetFolder
)

# 检查源文件夹是否存在
if (-not (Test-Path $SourceFolder)) {
    Write-Error "源文件夹不存在: $SourceFolder"
    exit 1
}

# 检查目标文件夹是否已存在
if (Test-Path $TargetFolder) {
    Write-Error "目标文件夹已存在: $TargetFolder"
    exit 1
}

# 获取源文件夹和目标文件夹的名称(不含路径)
$sourceName = Split-Path $SourceFolder -Leaf
$targetName = Split-Path $TargetFolder -Leaf

Write-Host "正在复制文件夹: $SourceFolder -> $TargetFolder"
Copy-Item -Path $SourceFolder -Destination $TargetFolder -Recurse

# 删除指定的文件夹
$foldersToDelete = @(".vscode", "Listings", "Objects")
foreach ($folder in $foldersToDelete) {
    $folderPath = Join-Path $TargetFolder $folder
    if (Test-Path $folderPath) {
        Write-Host "删除文件夹: $folder"
        Remove-Item -Path $folderPath -Recurse -Force
    }
}

# 删除 *.uvguix.* 文件
Write-Host "删除 *.uvguix.* 文件"
Get-ChildItem -Path $TargetFolder -Filter "*.uvguix.*" -File | Remove-Item -Force

# 重命名 .uvoptx 和 .uvprojx 文件
$uvFiles = @()
$uvFiles += Get-ChildItem -Path $TargetFolder -Filter "*$sourceName.uvoptx" -File
$uvFiles += Get-ChildItem -Path $TargetFolder -Filter "*$sourceName.uvprojx" -File

foreach ($file in $uvFiles) {
    $extension = $file.Extension
    $newName = "$targetName$extension"
    $newPath = Join-Path $TargetFolder $newName
    Write-Host "重命名文件: $($file.Name) -> $newName"
    Rename-Item -Path $file.FullName -NewName $newName
}

# 在所有文件中替换文本内容
Write-Host "替换所有文件中的文本: $sourceName -> $targetName"
$files = Get-ChildItem -Path $TargetFolder -Recurse -File

foreach ($file in $files) {
    # 跳过二进制文件和特定扩展名
    $binaryExtensions = @(".exe", ".dll", ".bin", ".hex", ".elf", ".o", ".obj", ".lib", ".a")
    if ($binaryExtensions -contains $file.Extension.ToLower()) {
        continue
    }
    
    try {
        $content = Get-Content -Path $file.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -ne $content -and $content -match $sourceName) {
            $newContent = $content -replace [regex]::Escape($sourceName), $targetName
            Set-Content -Path $file.FullName -Value $newContent -NoNewline
            Write-Host "  已更新: $($file.FullName.Substring($TargetFolder.Length))"
        }
    }
    catch {
        # 忽略无法读取的文件(可能是二进制文件)
    }
}

Write-Host "`n完成! 新工程文件夹: $TargetFolder" -ForegroundColor Green
Write-Host "提示: 可使用 VSCode 打开文件夹进行进一步检查"
