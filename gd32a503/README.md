## 新建工程

工作空间:

- 拷贝 `GD32A50x_Firmware_Library_V1.7.0\Firmware` 文件夹到工作空间
- 再新建一个存放工程的文件夹 `a503_empty`

工程:

![image-20251107164528095](README.assets/image-20251107164528095.png)

保存到工作空间的 `a503_empty` 文件夹下, 根据使用的封装在 GigaDevice 下选择 GD32A503x

勾选 CMSIS CORE

![image-20251107164943177](README.assets/image-20251107164943177.png)

Target 里面如果用printf可以勾选 Use MicroLIB

![image-20251107165228999](README.assets/image-20251107165228999.png)

Output 中勾选 Creat HEX File

![image-20251107165307380](README.assets/image-20251107165307380.png)

`C/C++` 选项卡下:

- 定义宏 `USE_STDPERIPH_DRIVER,GD32A50X`
- 选择 `AC5-Like Warnings`
- C99 C++11

![image-20251107165420454](README.assets/image-20251107165420454.png)

头文件包含:

![image-20251119111829353](README.assets/image-20251119111829353.png)

Asm 选择 `armasm (Arm Syntax)`

![image-20251107170042168](README.assets/image-20251107170042168.png)

```bash
--cpu Cortex-M33 -g --apcs=interwork --fpu=vfpv2 --pd "__MICROLIB SETA 1" 
-I .\RTE\_Target_1 
-I C:\z\app\Keil\Packs\ARM\CMSIS\6.2.0\CMSIS\Core\Include 
--pd "__UVISION_VERSION SETA 538" --pd "_RTE_ SETA 1" --pd "_RTE_ SETA 1" --list ".\Listings\*.lst" --xref -o "*.o" --depend "*.d" 
```

Linker 默认如下不改

```bash
--cpu=Cortex-M33 *.o
--library_type=microlib --strict --scatter ".\Objects\a503_blink.sct"
--summary_stderr --info summarysizes --map --load_addr_map_info --xref --callgraph --symbols
--info sizes --info totals --info unused --info veneers
--list ".\Listings\a503_blink.map"
-o .\Objects\a503_blink.axf
```

Debug 随调试器改, 此处不赘述

Utilities 先不改

Manage Project Items:

- CMSIS
  - "Firmware\CMSIS\GD\GD32A50x\Source\system_gd32a50x.c"
- Peripherals
  - "Firmware\GD32A50x_standard_peripheral\Source\" 文件夹下的所有 C 文件
- Startup
  - "Firmware\CMSIS\GD\GD32A50x\Source\ARM\startup_gd32a50x.s"
- Source
  - 自己的源文件 

如下图:

![image-20251119112458026](README.assets/image-20251119112458026.png)

编译无错误:

![image-20251119112614172](README.assets/image-20251119112614172.png)

## 重命名工程

拷贝 a503_empty 文件夹, 重命名为 a503_blink.

删除 .vscode Listings Objects 文件夹 和 `*.uvguix.*` 文件

重命名 `.uvoptx 和 .uvprojx` 文件为 a503_blink

VSCode 打开 a503_blink 文件夹, a503_empty  全部替换为 a503_blink, 这就要求工程名别致一些, 以免误替换.

![image-20251119114002467](README.assets/image-20251119114002467.png)

以上操作写成一个 copy_and_rename_project.ps1, 使用方法和效果如下:

![image-20251119114859703](README.assets/image-20251119114859703.png)



































