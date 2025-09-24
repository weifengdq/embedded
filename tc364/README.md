# TC364

## 板子图片

![image-20250924163350319](README.assets/image-20250924163350319.png)

![image-20250924163259415](README.assets/image-20250924163259415.png)

## 版本说明

- [Aurix Development Studio](https://www.infineon.cn/design-resources/platforms/aurix-software-tools/aurix-tools/aurix-development-studio) 1.10.16, 以下简称 ADS
- [Memtool](https://softwaretools.infineon.com/tools/com.ifx.tb.tool.infineonmemtool) 2025.02b
- [iLLD]([Releases · Infineon/illd_release_tc3x](https://github.com/Infineon/illd_release_tc3x/releases)) V1.20.1, 默认安装目录 `C:\Infineon\iLLD\TC3\v1.20.0`, API 文档 `"C:\Infineon\iLLD\TC3\v1.20.0\TC3-v1.20.0\Doc\TC3xx\iLLD_UM_TC3xx.chm"`
- 

## 设置 Release

工程右键 Properties:

![image-20250924170707292](README.assets/image-20250924170707292.png)

## 0_Board_Test_UART0

4M波特率打开CH343串口, 按下复位按钮后, 显示 `UART Echo Advanced Ready` 欢迎标语, 清空, 开始进行 1ms 周期定时发送, 每次 100 字节的回显, 一段时间后停止, 观察发送与接收计数:

![image-20250924175752819](README.assets/image-20250924175752819.png)

































