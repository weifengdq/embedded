# flash_rw_erase —— 片内 1MB Flash 读 / 写 / 擦与实测速度

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 用 **BootROM XPI NOR API**（rom_xpi_nor_*，hpm_romapi.h）操作片内 1 MB Flash；
- 读取 Flash 属性（容量/扇区/页大小）、解析 nor_cfg_option 与 boot_header；
- 在 **0x800F0000 起的最后 64 KB 安全区**做扇区擦除 → 编程 → 回读 → CRC32 校验，并实测三段耗时/速度；
- 打印程序镜像实际占用（etext），写之前做地址合法性检查（越界直接拒绝）；
- 注册 flashinfo/flashread/flasherase/flashrw 四个 shell 命令。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| Flash | **片内 1 MB**，HPM5E31 SIP 封装，经 XPI0 映射到 0x80000000（不是外接 NOR） |
| 启动介质 | 同一片 Flash，flash_xip 构建；J-Link 烧写 Bank0@0x80000000 |
| 安全写区 | 0x800F0000 ~ 0x800FFFFF（最后 64 KB），程序镜像只有 ~77 KB |
| 外部接线 | 不需要 |

## 编译与运行

```bash
bash scripts/build.sh flash/flash_rw_erase
bash scripts/hwrun.sh flash/flash_rw_erase --secs 12 --send "flashinfo;flashrw;flashrw 0x800f1000 4096"
```

## 命令

| 命令 | 说明 |
|---|---|
| flashinfo | Flash 属性 + 镜像占用 + nor_cfg_option/boot_header 解析 + 安全区 |
| flashread [addr] [len] | XIP 直读 hexdump（≤4096 B） |
| flasherase [addr] | 擦除一个 4 KB 扇区（只允许安全区内、扇区对齐） |
| flashrw [addr] [size] | 擦+写+读+CRC32 校验，默认 0x800F0000/4KB |

## 实测现象（串口输出）

日志：`logs/20261030-110952-flash_rw_erase.log`

```text
=== flash_rw_erase: internal 1MB flash via XPI0 ===
Flash @0x80000000 size=1024 KB (internal SIP NOR behind XPI0)
  sector=4096 B  page=256 B
  app image: 0x80000000 .. 0x80013708 (etext), used 77 KB of 1024 KB
  boot_header: tag=0xbf ver=0x10 len=144 fw_count=1
  fw[0]: offset=0x2000 size=0x10808(=67592 B) entry=0x80003000  # offset 相对 boot_header(0x1000), 实际 app@0x3000
  safe test window: 0x800f0000 .. 0x80100000 (last 64KB)
  nor_cfg_option @0x80000400: 80000400: 02 00 f9 fc 05 00 00 00 00 10 00 00 00 00 00 00
  boot header  @0x80001000: 80001000: bf 10 90 00 00 00 00 00 00 00 00 01 00 00 00 00
80001010: 00 20 00 00 08 08 01 00 00 00 00 00 00 00 00 00

letter:/$ flashrw
rw test @0x800f0000 size=4096 B (safe window, app image untouched)
  erase  1.872 ms -> 2137.0 KB/s, blank check: 0xFF ok
  write  14.279 ms -> 280.1 KB/s
  read   0.133 ms -> 30018.8 KB/s
  verify crc32 write=0x06beafd4 read=0x06beafd4 -> MATCH
  XIP direct read 0x800f0000 (after dcache invalidate): 800f0000: 00 1f 3e 5d 7c 9b ba d9 f8 17 36 55 74 93 b2 d1
```

| 操作 | 4 KB 耗时 | 折算速度 | 备注 |
|---|---|---|---|
| 扇区擦除 | 1.87 ms | 2.13 MB/s（≈547 扇区/s） | 擦除后空白校验 0xFF |
| 页编程 | 14.28 ms | 280 KB/s | 256 B/页，含 ROM API 开销 |
| 回读（ROM API） | 0.133 ms | 30.0 MB/s | XIP 直读更快（见第 21 章） |

## 关键点 / 易错点

1. **写之前必须确认镜像范围**：程序从 0x80000000 起执行，擦写自己所在的扇区会当场跑飞。本示例把可写范围硬编码为 [0x800F0000, 0x80100000)，并用 etext 打印镜像占用做交叉验证。
2. **ROM API 写完后 XIP 直读可能读到旧数据**：AXI 读 Flash 经过 L1 D-Cache，ROM API 的写不会自动失效 cache。必须 l1c_dc_invalidate() 后再直读，否则看到的是擦除后的 0xFF（第一次实测就遇到了，见日志 logs/20261030-110720-flash_rw_erase.log）。
3. **nor_cfg_option 与 BOARD_APP_XPI_NOR_CFG_OPT_OPT0 不一致**：board.c 里 flash_xip 用的 option[4] = {0xfcf90002, 0x00000005, 0x1000, 0}，而 board.h 的 BOARD_APP_XPI_NOR_CFG_OPT_OPT0 = 0x00000006。本示例用 board.h 的值(6)也能正常 auto_config，两者差异（probe type/读命令）建议以实际烧写进去的 0x80000400 内容为准。
4. **XPI NOR 不是"独立 FLASH 控制器"**：SDK 没有 FLASH IP 驱动，只有 rom_xpi_nor_*（BootROM）和 hpm_serial_nor（外接 SPI NOR 用）。片内 Flash 只能走 ROM API 或裸 XPI 寄存器。
5. J-Link 烧写会按 ELF 段擦写（日志里 "2 ranges affected (77824 bytes)"），所以本示例写进去的 0x800F0000 数据在下次烧写后会被恢复。
