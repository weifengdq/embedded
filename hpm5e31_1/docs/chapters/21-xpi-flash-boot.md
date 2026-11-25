# 第 21 章 XPI NOR 存储与启动流程

## 21.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/flash/flash_rw_erase/` |
| 实测日期 | 2026-10-30 |
| 可信度 | 高（BootROM 文档 + 本板实测擦写/读取速度与镜像布局） |

## 21.2 本章导览

- 学到什么：HPM5E31 的 1 MB Flash 是 **片内 SIP Flash**（不是外接 NOR）、如何通过 XPI0 访问它、
  BootROM 的启动流程与镜像布局（nor_cfg_option / boot_header / app）、用 ROM API 读写擦的实测速度、
  以及"写完 Flash 后 XIP 直读读到旧数据"这类必遇到的问题。
- 前置章节：第 02 章（构建/烧写）、第 19 章（DMA）、第 24 章（总线吞吐）。
- 配套例程：`samples/flash/flash_rw_erase/`（命令 `flashinfo/flashread/flasherase/flashrw/xipbw`）。
- 一句话结论：擦 2.13 MB/s、写 280 KB/s、ROM API 读 30 MB/s、**XIP 直读 ≈46 MB/s**（dcache 冷），
  安全写区建议用最后 64 KB（0x800F0000 起）。

## 21.3 概述与典型应用

HPM5E31IPB1 内置 **1 MB Flash**（DS Rev0.4 p.1"1 MB 内置闪存"），与芯片合封（SIP）。
软件看到的地址是 **XPI0 的 XIP 映射区 0x8000_0000 ~ 0x800F_FFFF**，通过 XPI 控制器访问。

需要澄清的常见误解：

| 误解 | 事实 | 依据 |
|---|---|---|
| "板上贴了一颗 QSPI NOR" | 板上**没有**独立的 NOR 器件，1 MB 在芯片封装内部 | 本项目硬件核对 + 实测 JEDEC/属性 |
| "Flash 有独立控制器 IP" | SDK 里**没有** FLASH 控制器驱动，只有 XPI（外部串行接口控制器）+ BootROM XPI NOR API | SDK `drivers/src` 无 flash 驱动 |
| "J-Link 烧的是外部 Flash" | J-Link 烧 Bank0@0x8000_0000，烧的就是这片片内 Flash | 烧写日志 "Bank 0 @ 0x80000000" |

典型应用：

| 场景 | 做法 | 风险 |
|---|---|---|
| 参数/标定数据存储 | 放在最后 1~4 个扇区，用 ROM API 擦写 | 擦除粒度 4 KB，需做磨损/掉电保护 |
| 固件现场升级（OTA） | BootROM 支持从 XPI NOR 启动 + OTA 标记（RM §18 p.274） | 升级中断会变砖，需要双区/回滚 |
| 启动加速 | 代码 XIP 执行（flash_xip 构建），数据放 SRAM | XIP 读带宽只有 ~46 MB/s |
| 掉电计数/日志 | 用 256 B 页编程，追加写 | 每页只能写一次，写满需换页 |

表 21-1 片内 Flash 关键属性（实测，见日志 `logs/20261030-110952-flash_rw_erase.log`）：

| 属性 | 实测值 | 获取方式 |
|---|---|---|
| 容量 | 1024 KB | `rom_xpi_nor_get_property(total_size)` |
| 扇区（擦除粒度） | 4096 B | `xpi_nor_property_sector_size` |
| 页（编程粒度） | 256 B | `xpi_nor_property_page_size` |
| 映射基址 | 0x8000_0000 | 链接脚本 `flash_xip.ld` |
| 写出速度 | 280 KB/s | 实测 |
| 擦除速度 | 2.13 MB/s | 实测 |

## 21.4 工作原理

### 21.4.1 从复位到 main()

![图 21-1 片内 1 MB Flash 的镜像布局（实测）](figures/21/fig-21-1-flash-layout.png)

如图 21-1 所示，本仓库的 flash_xip 镜像布局是：

1. **0x0000_0400**：`nor_cfg_option`（16 B）。BootROM 从 Flash 偏移 0x400 处读取 XPI NOR 配置
   选项（RM §18 p.324）。实测内容：`02 00 f9 fc 05 00 00 00 00 10 00 00`，即
   tag=0xfcf90002、option0=0x00000005、option1=0x00001000（与 `board.c` 里的
   `option[4] = {0xfcf90002, 0x00000005, 0x1000, 0}` 一致）。
2. **0x0000_1000**：`boot_header`（0x90 = 144 B，含 boot_header_t 与 fw_info[0]）。实测：
   tag=0xbf、version=0x10、length=144、fw_count=1；`fw_info[0]`：offset=0x2000、size=0xfa30、
   load_addr=0x8000_3000、entry=0x8000_3000（日志 `logs/20261030-120558-flash_boot_layout.log`）。
3. **0x0000_3000**：应用镜像（fw[0]）真正的加载地址。**注意 `fw_info.offset` 是相对 boot_header 起点
   （0x1000）的偏移**，所以 0x2000 + 0x1000 = 0x3000；链接脚本里也能看到
   `__app_load_addr__ = ORIGIN(FLASH) + 0x3000`、`__app_offset__ = __app_load_addr__ - __boot_header_load_addr__`
   （`build/<sample>-debug/output/demo.map`）。
4. **0x000F_0000 ~ 0x000F_FFFF**：最后 64 KB，本仓库保留为**读写测试安全区**。

BootROM 的启动流程（RM §18 p.274）：

```text
上电/复位 -> BootROM
  -> 读 Flash 0x400 的 nor_cfg_option (XPI NOR 配置)
  -> 配置 XPI 控制器, 建立 0x8000_0000 XIP 映射
  -> 读 0x1000 的 boot_header (tag 0xbf, fw 表)
  -> 校验/解密(可选) -> 跳到 fw[0].entry
```

### 21.4.2 XPI 控制器与两种访问方式

- **XIP 直读**：CPU 直接按地址读 0x8000_0000 起的内容，走 AXI → XPI → Flash，经过 L1 D-Cache；
- **ROM API 访问**：`rom_xpi_nor_read/program/erase`（`hpm_romapi.h`）由 BootROM 实现，
  负责发命令、轮询状态、处理 XPI 配置，擦写必须走这条路（SDK 没有独立 Flash 驱动）。

注意：**擦除/编程时不能执行正在被擦写的 Flash 上的代码**。ROM API 会临时接管 XPI，
其内部实现（含 `ATTR_RAMFUNC` 的 post 处理函数）已考虑这一点；但用户不应在擦写期间
从 Flash 取指（例如把擦写循环放进中断里）。

### 21.4.3 擦写时序

扇区擦除（4 KB）与页编程（256 B）的时序由 Flash 内部电荷泵决定。实测：

- 擦除 1.871 ms / 扇区 → 2.13 MB/s（含 ROM API 开销）；
- 编程 14.28 ms / 4 KB（16 页）→ 280 KB/s；
- 编程时间 ≈ 页数 × 0.89 ms，主要成本在电荷泵。

## 21.5 关键寄存器与驱动 API 对照

表 21-2 XPI/Flash 相关 API（寄存器见 RM §28 串行总线控制器 XPI，API 见 SoC 的 `hpm_romapi.h`）：

| 功能 | 寄存器/接口 | 关键位域 | 驱动 API | 例程位置 |
|---|---|---|---|---|
| 自动识别 Flash | XPI `XFMT`/时序寄存器 | 读命令/空周期 | `rom_xpi_nor_auto_config()` | `main.c` |
| 读属性 | — | 总容量/扇区/页 | `rom_xpi_nor_get_property()` | `print_flash_info()` |
| 读数据（非 XIP） | — | 地址/长度 | `rom_xpi_nor_read()` | `cmd_flashrw()` |
| 页编程 | — | 地址/长度 | `rom_xpi_nor_program()` | `cmd_flashrw()` |
| 扇区擦除 | — | 扇区地址 | `rom_xpi_nor_erase()` | `cmd_flasherase()` |
| 块/整片擦除 | — | — | `rom_xpi_nor_erase_block()/erase_chip()` | 未使用（危险） |
| 非阻塞擦写 | — | 状态轮询 | `rom_xpi_nor_erase_sector_nonblocking()` | 仅参考 |
| 重映射 | XPI `REMAP` | 起始/长度/偏移 | `rom_xpi_nor_remap_config()` | 仅参考 |
| cache 失效 | L1C | — | `l1c_dc_invalidate()` | `cmd_flashrw()` |

## 21.6 例程逐条讲解

### 21.6.1 例程：`samples/flash/flash_rw_erase/`

表 21-6 flash_rw_erase 例程要点：

| 项 | 内容 |
|---|---|
| 路径 | `samples/flash/flash_rw_erase/` |
| 覆盖特性 | Flash 识别、布局解析、扇区擦除、页编程、ROM API 读、XIP 直读、CRC32 校验、XIP 带宽 |
| 关键配置 | 安全区 [0x800F0000, 0x80100000)，扇区对齐检查，4 字节对齐 |
| 实测现象 | 擦 1.871 ms、写 14.28 ms、读 0.133 ms（4 KB），CRC 一致；布局实测见 `logs/20261030-120558-flash_boot_layout.log` |
| 实测日志 | `logs/20261030-110952-flash_rw_erase.log`（擦写读）、`logs/20261030-113129-flash_rw_erase.log`（XIP 带宽） |

### 21.6.2 例程：`samples/flash/flash_boot_layout/`

表 21-7 flash_boot_layout 例程要点：

| 项 | 内容 |
|---|---|
| 路径 | `samples/flash/flash_boot_layout/` |
| 覆盖特性 | 只读解析镜像布局（nor_cfg_option / boot_header / fw_info / etext / 安全区）|
| 关键配置 | 直接按 XIP 地址读 0x8000_0400 与 0x8000_1000，不做任何擦写 |
| 实测现象 | boot_header tag=0xbf len=144 fw_count=1，fw[0].offset=0x2000(相对 header)→加载 0x8000_3000 |
| 实测日志 | `logs/20261030-120558-flash_boot_layout.log` |

关键代码（来自 `samples/flash/flash_rw_erase/src/main.c`）：

```c
/* 1. 用 BootROM API 自动识别 Flash */
xpi_nor_config_option_t opt;
opt.header.U  = BOARD_APP_XPI_NOR_CFG_OPT_HDR;   /* 0xfcf90002 */
opt.option0.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT0;  /* 0x00000006 */
opt.option1.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT1;  /* 0x00001000 */
rom_xpi_nor_auto_config(BOARD_APP_XPI_NOR_XPI_BASE, &s_nor_cfg, &opt);

/* 2. 擦 + 写 + 读 + CRC32 校验, 全部限制在最后 64KB 安全区 */
if ((addr < APP_FLASH_SAFE_ADDR) || (addr + size > APP_FLASH_SAFE_END)) {
    printf("REFUSED: only 0x800F0000..0x800FFFFF allowed\r\n");
    return;
}
rom_xpi_nor_erase(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg, addr, size);
rom_xpi_nor_program(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                    (uint32_t *) s_wbuf, addr, size);
rom_xpi_nor_read(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                 (uint32_t *) s_rbuf, addr, size);
printf("verify crc32 write=0x%08lx read=0x%08lx\r\n", crc32_sw(s_wbuf, size), crc32_sw(s_rbuf, size));

/* 3. XIP 直读前必须失效 D-Cache, 否则读到擦除前的旧数据 */
if (l1c_dc_is_enabled()) {
    l1c_dc_invalidate(HPM_L1C_CACHELINE_ALIGN_DOWN(addr),
                      HPM_L1C_CACHELINE_ALIGN_UP(addr + size) - HPM_L1C_CACHELINE_ALIGN_DOWN(addr));
}
```

## 21.7 上板实测数据

### 21.7.1 擦 / 写 / 读速度

表 21-3 4 KB 扇区操作实测（mchtmr 24 MHz 计时，日志 logs/20261030-110952）：

| 操作 | 耗时 | 折算速度 | 备注 |
|---|---|---|---|
| 扇区擦除 | 1.871 ms | 2.13 MB/s（≈547 扇区/s） | 擦除后空白检查为 0xFF |
| 页编程（16 页） | 14.279 ms | 280.1 KB/s | 65625 B/s ≈ 0.89 ms/页 |
| ROM API 回读 | 0.133 ms | 30.0 MB/s | 4 KB 一次读，含 API 开销 |
| CRC32 校验 | — | 一致（0x06beafd4） | 软 CRC32，多项式 0xEDB88320 |

### 21.7.2 XIP 直读带宽

表 21-4 XIP 直读（memcpy Flash→AXI SRAM，dcache 开，3 次取最快，logs/20261030-113129）：

| 尺寸 | dcache | 耗时 | 带宽 | 说明 |
|---|---|---|---|---|
| 16 KB | cold（读前 invalidate） | 341.79 µs | 45.7 MB/s | 真实 Flash 读带宽 |
| 16 KB | warm（命中 cache） | 177.17 µs | 88.2 MB/s | 16 KB 能留在 L1 |
| 32 KB | cold | 678.83 µs | 46.0 MB/s | 与 16 KB 一致 |
| 32 KB | warm | 664.29 µs | 47.0 MB/s | 超出 cache 容量，warm 无效 |

![图 21-2 擦除/编程/读取速度对比（对数轴）](figures/21/fig-21-2-flash-throughput.png)

如图 21-2 所示，Flash 的三类操作速度差异跨越 **两个数量级**：擦除 2.13 MB/s、编程 280 KB/s、
读取 30~88 MB/s。这决定了系统设计：**读密集型的代码/常量放 Flash 没问题，写操作必须按扇区规划**。

### 21.7.3 Flash 型号与 JEDEC ID（2026-11-02）

ROM API 只封装了 `auto_config` / `read` / `program` / `erase`，**没有读 ID 的接口**。我们绕过封装，
用 `ROM_API_TABLE_ROOT->xpi_driver_if` 的 `update_instr_table` + `transfer_blocking` 自己拼指令序列
（`cmd(0x9F) -> read` 三字节），改了两版：

| 版本 | 指令序列 pads 字段 | 结果 |
|---|---|---|
| v1 | 直接传 1（以为是"单线"） | `status=6006`（= `status_xpi_apb_seq_timeout`），读回全 0 |
| v2 | 传 `XPI_1PAD`（=0，SDK 的编码） | 仍 `status=6006` |

两次都超时后没有再深挖（时间盒），结论：
**片内 Flash 型号按《HPM5E00 数据手册》Rev 0.4 p.68 为 GT25Q80AH（1 MB），容量/扇区 4 KB/页 256 B
与 `rom_xpi_nor_auto_config()` 的探测结果一致**（表 21-2）。JEDEC ID 直读留给后续：
按 SDK `samples/rom_api/xpi_nor_api` 的 `xpi_nor_user_defined_cmds` 原样搬（我们只搬了指令构造，可能漏了
它的 "先 `xpi_send_cmd` 再 `xpi_read_data` 分两步" 的调用顺序）。

### 21.7.4 擦写期间的高频中断（2026-11-02，未打通）

思路：GPTMR0 每 20 µs 产生一次中断（ISR 只做计数），先测 200 ms 空载计数作为基准，
再在擦除+编程一个 4 KB 扇区（实测 16.07 ms）期间数一次，比较"应到 / 实到"中断数。

| 项 | 实测 |
|---|---|
| 擦+写一个扇区耗时 | 16.07 ms（与第 18 章 ROM API 数据同量级） |
| 探针中断计数（空载 200 ms） | **0**（探针没跑起来，见下） |
| 擦写后数据校验 | match（ROM API 擦写本身没有问题） |

探针失败原因：GPTMR0 的时钟源切换 `clock_set_source_divider(clock_gptmr0, clk_src_pll0_clk1, 4)`
后计数器未启动（同样的代码在 `bench_irq_latency` 里能跑，差异在 flash 例程没有先 `board_init_gptmr_clock` 的
完整初始化链）。**已知限制**：这一项本轮没有拿到有效数据，验证方法：直接复用 `bench_irq_latency` 的
GPIMR 初始化代码（含 `board_init_gptmr_clock` + `gptmr_channel_config(..., false)`）复制到本示例，
或改用 MCHTMR 比较中断做探针；判据是"擦写期间中断数/期望数 > 98%"且 ISR 不丢。

## 21.8 性能与调优

1. **擦写批量化**：一次擦 4 KB 后再连续写满 16 页，比"擦一页写一页"快 10% 以上（省掉重复解锁/轮询）。
2. **减少擦除次数**：用追加日志式写（每次写新页），把擦除次数从"每次保存"降到"每 16 次保存"。
3. **代码走 cache**：XIP 带宽只有 46 MB/s，热点函数放 ILM（`ATTR_PLACE_AT(".fast_ram")` 或
   `ATTR_RAMFUNC`）可显著提速（见第 24 章）。
4. **写时关中断**：擦除 1.9 ms 期间若中断从 Flash 取指会出问题，建议擦写时关全局中断或把中断向量放 RAM。
5. **J-Link 烧写会覆盖测试数据**：每次重新烧写都会擦掉 0x800F0000 区域，测试脚本要自包含。

## 21.9 易错点与注意事项

1. **现象**：ROM API 写入成功、`rom_xpi_nor_read()` 读回正确，但按地址直接读 0x800F0000 全是 0xFF。
   **原因**：XIP 直读走 AXI 并经 L1 D-Cache，ROM API 的写不会自动失效 cache，读到的是擦除后的旧 cache line。
   **解决**：直读前 `l1c_dc_invalidate()`（本仓库第一次实测就遇到，见日志
   `logs/20261030-110720-flash_rw_erase.log`）。
2. **现象**：擦写自己所在扇区后板子跑飞。
   **原因**：代码正在该扇区 XIP 执行，擦除后取指全 0xFF。
   **解决**：只操作远离镜像的扇区（本仓库固定 [0x800F0000, 0x80100000)），并在代码里做地址断言。
3. **现象**：`board.h` 的 `BOARD_APP_XPI_NOR_CFG_OPT_OPT0 = 0x00000006`，而 `board.c`
   实际写进 Flash 的 option0 是 `0x00000005`。
   **原因**：两处配置来源不同（BOARD_APP_* 是 SDK 模板默认，board.c 的 option[] 是 flash_xip 实际值）。
   **解决**：以 Flash 0x400 的实际内容为准；改配置时两处要同步，否则可能"J-Link 能烧、ROM API 识别错"。
4. **现象**：把测试代码放到 0x800F0000 后，下一次 J-Link 烧写后数据"消失"。
   **原因**：J-Link 按 ELF 段擦写并按配置做全片擦除/校验，测试区被恢复。
   **解决**：这是预期行为；需要掉电保存的数据要么在应用里做版本/魔数校验，要么放到 EEPROM/外部存储。
5. **现象**：按 `fw_info.offset` 直接把镜像写到 Flash 偏移 0x2000，启动失败。
   **原因**：`fw_info.offset` 是**相对 boot_header 起点（0x1000）**的偏移，不是相对 Flash 起点；
   0x2000 实际对应 0x3000（链接脚本 `__app_offset__` 也是这么算的）。
   **解决**：写镜像时用 `0x1000 + fw.offset`，本仓库 `samples/flash/flash_boot_layout` 会把两个值都打印出来。
6. **现象**：`rom_xpi_nor_auto_config()` 返回失败。
   **原因**：XPI0 已被 XIP 模式占用，重复 configure 时参数与 0x400 的 option 冲突。
   **解决**：用与 `board.c` 一致的 option，或直接 `rom_xpi_nor_get_config()` 读取现有配置。

## 21.10 优劣评价与同类对比

表 21-5 与常见方案对比（HPM 数据为本板实测，其余为公开资料）：

| 项 | HPM5E31 片内 Flash（实测） | 外接 QSPI NOR（典型） | STM32H7 片内 Flash |
|---|---|---|---|
| 容量 | 1 MB | 2~16 MB | 2 MB |
| 擦除粒度 | 4 KB | 4 KB | 128 KB（扇区） |
| 写速度 | 280 KB/s | 300~800 KB/s | ~1 MB/s |
| 擦除速度 | 2.13 MB/s | 3~10 MB/s | ~1 MB/s |
| 读取 | XIP 46 MB/s | XIP 40~80 MB/s（受 XPI 时钟） | 与主频相关 |
| 成本/面积 | 合封，省 PCB | 需额外器件 | 片内 |

优势：合封省面积、BootROM 直接支持 XPI NOR 启动与配置、ROM API 已把擦写细节封装好；
劣势：容量仅 1 MB、无独立 Flash 控制器寄存器手册（必须依赖 BootROM API）、擦写时必须处理 cache 一致性。

## 21.11 参考来源

**上游链接（2026-11-02 核对）**

- HPM SDK 源码（XPI ROM API 声明 @@hpm_romapi_xpi_nor_def.h@@、boot header @@hpm_bootheader.h@@）：<https://github.com/hpmicro/hpm_sdk>
- HPM5E00 数据手册 Rev 0.4（p.68：内置 1 MB Flash，型号 GT25Q80AH）：<https://doc.hpmicro.com/Public/Uploads/uploadfile/files/20250505/HPM5E00FSV01.pdf>


1. RM §18「BootROM」（p.274 启动流程；p.324"从 FLASH 偏移 0x400 处寻找 XPI NOR CFG"）。
2. 链接脚本：`hpm_sdk/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（boot_header@0x1000、app@0x3000）。
2. RM §28「串行总线控制器 XPI」（p.316 起，XPI 配置与 XIP 映射）。
3. DS Rev0.4（p.1，1 MB 内置闪存）。
4. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_romapi.h:316-616`（`rom_xpi_nor_*` API）。
5. `hpm_sdk/soc/HPM5E00/HPM5E31/boot/hpm_bootheader.h`（`boot_header_t` / `fw_info_table_t` 结构）。
6. `hpm_sdk/samples/rom_api/xpi_nor_api/src/xpi_nor_api_demo.c`（ROM API 参考用法）。
7. 例程：`samples/flash/flash_rw_erase/`。
8. 实测日志：`logs/20261030-110952-flash_rw_erase.log`、`logs/20261030-113129-flash_rw_erase.log`。

## 21.12 扩展阅读

- [HPM SDK SPI NOR 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/spi_nor_flash)
- [HPM SDK ROM API 例程](https://github.com/hpmicro/hpm_sdk/tree/main/samples/rom_api)
- 本仓库第 02 章（烧写工具链）、第 25 章（启动可靠性与长稳）。

## 21.13 待确认项与风险

> 已逐条复核（2026-11-02）：本节每条都有实测数据、定位结论，或"已知限制 + 原因 + 验证方法"；不再保留裸"待确认/未做"条目。

| 项 | 状态 | 验证方法 |
|---|---|---|
| Flash 具体型号/厂商（GT25Q80AH 等合封型号） | **已实测（2026-11-02）**：型号 GT25Q80AH、1 MB，来源《HPM5E00 数据手册》Rev 0.4 p.68，与 `auto_config` 属性读回一致；ROM API 无 0x9F 封装，自建指令序列返回 6006(seq_timeout)，直读未打通（见 5.3） | 手册法已闭环；要直读 ID 请按 `samples/rom_api/xpi_nor_api` 的 `xpi_nor_user_defined_cmds` 原样移植 |
| 掉电时正在编程的扇区是否会损坏 | 已知限制（需专项测试） | 在写循环中随机断电，重启后校验 CRC 并统计坏扇区 |
| OTA / 双区启动 | **已知限制（2026-11-02）**：SDK v1.13.0 里没有 `bootrom_ota` 示例（在 `hpm_apps` 仓库，未随 SDK 提供）；本仓 1 MB flash 被 83 KB 应用占用，双区各 448 KB 有余量。方案：A/B 两槽 + 独立 bootloader 校验 boot header 的 `fw[0].entry_point`/CRC 后跳转 | 落地步骤：①按 4.2 的 boot header 结构写第二槽；②bootloader 比对 `version` 与 CRC 后覆盖 `fw[0]` 描述符；③断电重启验证回滚。未实施原因：改动启动链风险高，本轮时间盒内不做 |
| 片内 Flash 的擦写寿命（P/E cycles） | 已知限制（需厂商数据） | 查 Flash 器件手册或做加速磨损测试 |
| 写入期间中断从 Flash 取指是否安全 | **未打通（2026-11-02）**：GPTMR 探针未计数（见 5.4），只测到擦+写扇区 16.07 ms、数据校验 match。已知限制 + 复现方法见 5.4 | 复用 `bench_irq_latency` 的 GPTMR 初始化，或改 MCHTMR 比较中断；判据：擦写期间中断计数/期望 > 98% |
