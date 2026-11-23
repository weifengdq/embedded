# flash_boot_layout —— 片内 Flash 启动镜像布局（只读解析）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 只读解析片内 1 MB Flash 的启动布局：`nor_cfg_option`(0x400)、`boot_header`(0x1000)、
  `fw_info[0]`、应用镜像实际加载地址、预留安全区；
- 打印布局表与 `dump [offset] [len]` 十六进制查看；
- **不做任何擦写**，可以安全地在任何例程上跑（本仓库用它做第 21 章的布局证据）。

## 硬件要求 / 接线

无。Flash 是片内 1 MB（HPM5E31 SIP），经 XPI0 映射到 0x8000_0000。

## 编译与运行

```bash
bash scripts/build.sh flash/flash_boot_layout
bash scripts/hwrun.sh flash/flash_boot_layout --secs 10 --send "layout;dump 0x400 64;dump 0x1000 48"
```

## 命令

| 命令 | 说明 |
|---|---|
| layout | 打印布局表与 boot_header/fw_info 解析结果 |
| dump [offset] [len] | 从 0x8000_0000+offset 起 hexdump（≤1024 B） |

## 实测现象（串口输出）

日志：`logs/20261030-120558-flash_boot_layout.log`

```text
Flash base=0x80000000 size=1024 KB
  +0x0400 nor_cfg_option : fcf90002 00000005 00001000 00000000
  +0x1000 boot_header    : tag=0xbf ver=0x10 len=144 fw_count=1 dc_off=0 sig_off=0
  fw[0]: offset=0x2000  size=0x0fa30 (64048 B)  entry=0x80003000
  app image: 0x80000000..0x800129b0 (etext, 74 KB), boot header says app at +0x2000
  safe rw window: +0xf0000..+0xFFFFF (64 KB)

  布局表:
  | 偏移        | 大小      | 内容                     |
  |-------------|-----------|--------------------------|
  | 0x00000     | 0x400     | 保留                     |
  | 0x00400     | 0x10      | nor_cfg_option (XPI 配置)|
  | 0x01000     | 0x90      | boot_header + fw[0]      |
  | 0x02000     | 0x0fa30   | 应用镜像 fw[0]           |
  | 0x11a30     | 0xde5d0   | 未使用                   |
  | 0xf0000     | 0x10000   | 读写测试安全区           |

letter:/$ dump 0x1000 48
80001000: bf 10 90 00 00 00 00 00 00 00 00 01 00 00 00 00
80001010: 00 20 00 00 30 fa 00 00 00 00 00 00 00 00 00 00
80001020: 00 30 00 80 00 00 00 00 00 30 00 80 00 00 00 00
```

## 关键点 / 坑（重要）

- `fw_info[0].offset = 0x2000` **不是**"应用在 Flash 偏移 0x2000"，而是**相对 boot_header 起点(0x1000)**
  的偏移：实际加载地址 = 0x1000 + 0x2000 = **0x8000_3000**，与 `load_addr/entry=0x80003000` 一致。
  链接脚本 `flash_xip.ld` 里 `__app_offset__ = __app_load_addr__ - __boot_header_load_addr__` 就是这个含义。
- 0x1000 的 0x90 字节 = `boot_header_t`(16 B) + `fw_info_table_t`(0x50 B) + 哈希/IV 区域（本工程 hash 全 0，未签名）。
- `nor_cfg_option` = `fcf90002 00000005 00001000 00000000`（tag/option0/option1/option2），
  与 `board.c` 的 `option[4]` 一致；`board.h` 的 `BOARD_APP_XPI_NOR_CFG_OPT_OPT0=6` 是 SDK 模板默认值，
  两者不一致（见第 21 章坑 3）。
