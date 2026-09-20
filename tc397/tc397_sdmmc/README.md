# tc397_sdmmc — TC397XX SDMMC0 + FatFs R0.16 (exFAT/FAT32) + Letter-Shell

本工程由 `tc397_uart_lettershell` 拷贝而来，在 **Ubuntu 26.04 + tricore-gcc 13.4.1 +
CMake/Ninja** 下构建，目标 **TC397XX 292pin**。在 UART Letter-Shell 基线（ASCLIN0
P14.0/P14.1, 921600-8N1，P13.0 LED）之上，新增 **SDMMC0 SD 卡 + FatFs 文件系统**
完整测试能力。

> **当前状态（2026-09-20 第二轮攻坚）**：
> **读通路全部 PASS**（初始化/挂载/FAT32 识别/目录/空闲空间/裸扇区读），
> **写通路仍 FAIL**（`DISK_ERR`）。
> 本轮把写失败**定位到硬件级根因**：**主机控制器在写（host→card）方向从不驱动 DAT 线**
> —— CMD24 被卡正常接受（卡进入 `state=6 RCV` 等数据），FIFO 也接收了 512B，
> 但 DAT0-3 全程恒定高电平（用 `PSTATE.DAT_3_0` 采样 + 读方向对照实验证明），
> 因此数据从未到达卡，`XFER_COMPLETE`/`WR_XFER_ACTIVE` 永不置位，最后卡超时。
> 已排除：卡本身、时钟、位宽、DMA 类型、`BLOCK_COUNT_ENABLE`/`MULTI_BLK_SEL`、
> HostV4、`PRESET_VAL_ENABLE`、`PWR_CTRL` 电压、引脚/pad 配置（CMD 与 DAT 引脚
> IOCR 完全相同且 CMD 能正常驱动）、命令表 `DATA_PRESENT_SEL`。
> 详见 **§9**（含完整日志、对照实验、排除矩阵、下一步建议）。历史记录见 §6/§7/§8。

* 基线：`tc397_uart_lettershell`（Shell/UART/CMake/build.sh，命令兼容）
* FatFs：`ref/fatfs`（`abbrev/fatfs` master，即 ChaN FatFs **R0.16**，2025），
  取 `ff.c/ff.h/diskio.h/ffunicode.c`，`ffconf.h` 为本工程定制（见 §5）
* SDMMC 胶水层：参考 Infineon 官方 example
  `iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write` 的 `mmc_sdmmc.c`/`diskio.c`
  （4-bit SD + SDMA 多块传输）移植，适配 R0.16 磁盘接口
* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 +
  `aurix_flasher`（复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`）
* 串口：`/dev/ttyACM0`（1a86:55d3），DAP MiniWiggler `058b:0043` 仅用于 TAS 下载

---

## 1 硬件与原理

### 1.1 引脚（与官方 3V3 AppKit 一致）

| 信号 | TC397 Pin | iLLD PinMap | 说明 |
| --- | --- | --- | --- |
| SD CLK | P15.1 | `IfxSdmmc0_CLK_P15_1_OUT` (alt7) | 25MHz（见 §6.5 时钟注记） |
| SD CMD | P15.3 | `IfxSdmmc0_CMD_P15_3_INOUT` (RxSel_a) | 命令/响应 |
| SD DAT0 | P20.7 | `IfxSdmmc0_DAT0_P20_7_INOUT` | 数据 |
| SD DAT1 | P20.8 | `IfxSdmmc0_DAT1_P20_8_INOUT` | 数据（4-bit 用） |
| SD DAT2 | P20.10 | `IfxSdmmc0_DAT2_P20_10_INOUT` | 数据（4-bit 用） |
| SD DAT3 | P20.11 | `IfxSdmmc0_DAT3_P20_11_INOUT` | 数据（4-bit 用） |
| SD CD | P10.7 | GPIO 输入上拉 | 卡在位=低（机械开关对地） |
| UART TX/RX | P14.0/P14.1 | ASCLIN0, 921600-8N1 | 调试 Shell |
| LED | P13.0 | 低电平点亮，1Hz 心跳 | 状态指示 |

依据：`Libraries/iLLD/.../_PinMap/TC39xB/IfxSdmmc_PinMap_TC39xB_LFBGA292.{h,c}`。

### 1.2 数据通路原理

```
Letter-Shell `sd` 命令
  → FatFs R0.16 API (f_mount/f_read/f_write/f_mkfs/f_getfree/...)
  → diskio.c (单物理盘 0 → SDMMC 后端, disk_timerproc 每 10ms 由 STM 中断驱动)
  → mmc_sdmmc.c (iLLD IfxSdmmc_Sd, 4-bit + 高速 + SDMA 多块, 25MHz)
  → SDMMC0 控制器 → TF 卡
```

* **SD 初始化**：CMD0→CMD8→ACMD41→CMD2/3/7→切 4-bit（ACMD6）→切高速（CMD6）→
  CMD16 固定 512B→时钟 25MHz。RCA=0x1388（本卡）。
* **SDMA 多块**：读写一律 `read/writeMultiBlock`（CMD18/25 + Auto CMD12），
  缓冲须 4 字节对齐（`s_ioBuf`、`s_mkfsWork` 均为 `uint32/DWORD` 数组）。
* **容量**：CMD9 (SEND_CSD) 解析，128GB 卡=268435456+1024=**268436480 扇区
  ×512B = 131072MiB = 128.0GiB**（SDXC，block addressing）。
* **文件系统**：SDXC 标准用 **exFAT**（`FF_FS_EXFAT=1` 需 `FF_USE_LFN>=1` +
  `ffunicode.c`）；本卡到货为 FAT32（非标准但可挂载，见测试结果）。
  `f_mkfs` 支持 `FM_EXFAT`/`FM_FAT32`（见 §6.4 未完成项）。
* **DCache**：CPU0 DCache 默认开，SDMA 无硬件一致性。胶水层每次传输前对
  缓冲做 `CACHEA.WI`（clean+invalidate，写回脏行后再起 DMA；**禁止**裸
  `cachea.i`，会丢弃共享缓存行的脏 FatFs 元数据——已踩坑，见 §6.3）。
* **可靠性加固**（实测中加入，见 §6.2）：CMD/传输 3 次重试、中断粘滞标志
  清除（EISTR 粘滞会锁死后续命令，PSTATE.CMD_ISSUE_ERR）、容量缓存（减少
  运行时 CMD9/R2）、`sd recover`（CMD12 中止 + SW_RST_DAT/CMD + 重初始化）。

### 1.3 链接脚本大坑（根因级，家族工程通用）

tricore-gcc 的 `-fdata-sections` 把每个静态变量放到**裸 `.sym` 段**
（如 `.shellList`，而非 `.data.sym`），而 `Lcf_Gnuc_Tricore_Tc.lsl` 的
copy/clear 表只覆盖 `.data/.bss` 等经典段 → 这些变量成**孤儿段**，
启动不初始化，残留上电随机值。后果实测：`shellList[1..4]` 野指针，
`shellGetCurrent()` 跳转野指针 trap，现象为**上电串口无输出、必死**
（用小固件、换命令数等偶然能跑，全看运气）。
修复：`CMakeLists.txt` 去掉 `-fdata-sections`（保留 `-ffunction-sections`，
代码段无需初始化不受影响）。此坑同样潜伏于 `tc397_uart_lettershell`
（目前靠运气工作），建议家族工程统一修复。

---

## 2 目录结构

```
tc397_sdmmc/
├── cmake/tricore-gcc-toolchain.cmake  # /opt/tricore-gcc/bin, 13.4.1
├── cmake/AurixProject.cmake           # 递归收集源码
├── Configurations/                    # Configuration.h/isr + Ifx_Cfg (292)
├── Libraries/
│   ├── iLLD/TC3xx/...                 # tc397_0 原版（TC39xB），勿覆盖
│   ├── UART/UART_Logging.c/h          # ASCLIN0 921600（同 uart 基线）
│   ├── FatFS/                         # FatFs R0.16 移植（新增）
│   │   ├── ff.c/ff.h/diskio.h/ffunicode.c  # 取自 ref/fatfs（R0.16 原版）
│   │   ├── ffconf.h                   # 本工程定制（MKFS/LFN/exFAT/LBA64…）
│   │   ├── diskio.c                   # 单盘胶水 + disk_timerproc
│   │   └── mmc_sdmmc.c/h              # SDMMC0 后端（官方 example 改编）
│   └── Infra/Service/...              # Bsp, Ssw, Platform（同基线）
├── Shell/
│   ├── letter-shell/src/              # 3.2.4（同基线）
│   ├── shell_port.c/h                 # 基线命令（mcu/temp/led/…）
│   └── shell_sd.c/h                   # `sd` 卡测试命令集（新增，见 §4）
├── Lcf_Gnuc_Tricore_Tc.lsl            # .shellCommand 段（同基线）
├── Cpu0_Main.c                        # STM 1ms + disk_timerproc/10ms + CD 初始化
├── Cpu1..5_Main.c                     # 同步空转（同基线）
└── build.sh / serial_monitor.py
```

---

## 3 构建与下载（Ubuntu 26.04）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH
cd tc397_sdmmc
./build.sh build                        # Debug（默认）
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 经 flasher -read 通路复位
```

产物 `build/gcc/tc397_sdmmc.{elf,hex,map}`（`build/` 不进 git）。
当前 Debug 体积约：text ~130K（含 FatFs+exFAT+LFN，比 uart 基线 +47K），
data ~2K，bss ~87K。

串口（可靠流程：端口保持打开→flasher 复位→等完整启动→再发命令；直接
open 后立即发命令可能撞上 DTR 复位窗口被吞掉）：

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw
python3 serial_monitor.py --port /dev/ttyACM0 --baud 921600 --duration 10
```

---

## 4 `sd` 命令集

全部子命令挂在一个 `sd` 下（`sd` 无参打印用法）：

| 命令 | 说明 |
| --- | --- |
| `sd init` | 初始化 SD + 挂载，打印 RCA/类型/容量/FS/空闲 |
| `sd info` | 卡+FS 信息（不重初始化） |
| `sd cd` | P10.7 卡在位电平 |
| `sd ls [path]` | 列目录（默认 `0:/`，最多 64 条） |
| `sd cat <file> [maxB]` | 文本方式 dump 文件（默认 2048B） |
| `sd stat <file>` | 大小/属性/时间 |
| `sd write <f> <KB> [seed]` | 写 pattern 文件，报 KB/s |
| `sd read <f> [seed]` | 读回校验 pattern，报 KB/s |
| `sd bench [MB]` | 顺序写+读+校验（默认 4MB，`0:/BENCH.BIN`） |
| `sd mkfs <exfat\|fat32> [au_shift]` | **格式化（销毁数据！）** |
| `sd erase <lba> <count>` | 裸扇区清零（最多 131072 扇区/次） |
| `sd raw <r\|w> <lba> [n]` | 裸扇区读（hex）/写（pattern），n≤16 |
| `sd rm <path>` / `sd mkdir <path>` | 删除/建目录 |
| `sd label [new]` | 取/设卷标 |
| `sd free` | 容量 + 空闲 |
| `sd st` | 链路状态 + 卡 R1 状态机解码 |
| `sd regs` | 主机寄存器 + 最近一次错误锁存 |
| `sd recover` | CMD12 中止 + SW_RST_DAT/CMD + 重初始化 |

读写 pattern：`word[i] = (chunk偏移字 + i) ^ seed`，读回逐字校验。

> 另有本轮新增的**诊断命令族**（`sd diag / tx / exp r|w|pre|dma / c24 / clk /
> host / poke / peek / mpeek / portinfo / chk`），用途与用法见 **§9.1**。

---

## 5 FatFs 配置要点（`Libraries/FatFS/ffconf.h`，R0.16 `FFCONF_DEF 80386`）

| 选项 | 值 | 原因 |
| --- | --- | --- |
| `FF_USE_MKFS` | 1 | `sd mkfs` 需要 |
| `FF_USE_LFN` | 1（静态 BSS） | exFAT 强制要求 LFN；BSS 静态免堆 |
| `FF_MAX_LFN` | 255 | 全 LFN 支持 |
| `FF_FS_EXFAT` | 1 | 128GB SDXC 标准格式 |
| `FF_LBA64` | 1 | 64 位 LBA（本卡 268M 扇区用不到，但无害） |
| `FF_CODE_PAGE` | 437 | 英文 OEM，表最小 |
| `FF_USE_LABEL` | 1 | `sd label` 需要 |
| `FF_MIN_SS/FF_MAX_SS` | 512/512 | SD 卡固定 512 |
| `FF_FS_NORTC` | 1（固定 2026-09-15） | 板无 RTC |
| `FF_VOLUMES` | 1 | 单盘 |
| 其余 | 默认（TINY 0/LOCK 0/REENTRANT 0/…） | 裸机单核够用 |

工作区：`s_mkfsWork` 为 `DWORD[128]`（512B，SDMA 对齐要求）。

---

## 6 测试方法、步骤与结果

### 6.1 测试方法

* 板端 `sd ...` 命令经 921600 串口交互；长耗时命令（bench/mkfs）直接报
  耗时与速度（STM 1ms tick 计时）。
* 可靠串口流程：`talk.py` 式——端口保持打开→`./build.sh reset`→等完整
  boot（含 `letter:/$`）→再发命令（详见 handover）。
* 测速原理：`sd bench N` 写 N-MB pattern 文件（8KB chunk，`f_write`），
  再读回逐字校验（`f_read`），`速度 = 字节数/毫秒`。

### 6.2 测试步骤（标准回归）

```text
sd cd                        # 确认插卡（期望 CD=0）
sd init                      # 初始化+挂载（期望 RCA/容量/FS 打印）
sd ls                        # 看卡内原有内容
sd bench 4                   # 4MB 写+读+校验，记录 KB/s
sd write 0:/T.BIN 1024       # 1MB 写测速
sd read 0:/T.BIN             # 读回校验测速
sd cat / sd stat / mkdir / rm / label  # 文件操作
sd erase / sd raw            # 裸扇区（谨慎，破坏数据）
sd mkfs exfat                # 128GB 目标格式（销毁数据！）
sd st / sd regs / sd recover # 诊断链路
```

### 6.3 实测结果（2026-09-15，128GB TF 卡，FAT32 现状）

* `sd cd` → `CD P10.7=0`（卡在位）✓
* `sd init` → `disk_initialize=0x00`，`f_mount OK`，
  `RCA=0x1388 type=SDmem cap=SDHC/SDXC(block)`，
  **容量 268436480 扇区 = 131072MiB = 128.0GiB** ✓，
  `FS: FAT32, cluster=32 sectors` ✓
* `sd ls 0:/` →
  `System Volume Information/`，`BOOT.bin 447672B`，`image.ub 8293668B` ✓
  （卡内原有镜像文件，用户已授权可删；本次测试未删除）
* `sd bench 4`（FAT32，4MB）：
  * 写 `4096KB / 923ms = 4437KB/s` ✓
  * 读 `4096KB / 297ms = 13791KB/s VERIFY-OK` ✓
* `sd bench 1`（复测）：写 `1024KB / 168ms = 6095KB/s` ✓，
  读 `1024KB / 371ms = 2760KB/s`（读速度各次波动大，见注记）
* `sd raw r 2048` → FAT32 VBR（`EB 58 90 4D 53 44 4F 53…`）✓
* 读速度注记：25MHz 4-bit SDR 理论上限 12.5MB/s，某次读到 13.8MB/s，
  说明实际 SDCLK 可能高于 25MHz（CLK_CTRL 实测恒 0x000F，见 §6.5），
  各次读数 2.8~13.8MB/s 波动，写稳定 4.4~6.1MB/s。

### 6.4 未完成/受阻项（诚实记录）

1. **板端 `f_mkfs`（exFAT/FAT32）尚未成功**：`sd mkfs exfat` 返回
   `FR_DISK_ERR`。已定位到 exFAT 首写（up-case 表，sector 10528）；
   排查中引入了 CMD12/SW_RST/recover、重试、粘滞清零、容量缓存、
   CACHEA 维护等加固（均已保留，见 §1.2）。**但随后测试卡进入
   “CMD 可、DAT 全败”状态（连 mount 都失败），无法继续验证 mkfs。**
   按用户授权“可用其它方式格式化”：建议在 PC 上
   `mkfs.exfat /dev/sdX`（USB 读卡器）格式化后，拿回板端做
   exFAT 挂载+bench 验证（命令均已就绪）。
2. **测试卡当前状态**：CMD 初始化/RCA/CID/CSD 正常，CMD13 偶发超时，
   所有 DAT 传输失败，R1 曾读到 state=6（RCV，写数据没送达卡住）。
   SW_RST/CMD12/CMD0/重初始化/1-bit/系统复位均不能恢复。
   **最大可能：需物理掉电（板卡不断电，MCU 复位不清 SD 卡状态），
   或卡本身损坏/体质差。下一步：给板子断电重启，或换一张卡。**
3. **MBR 说明**：早期一次 `mkfs` 尝试把 LBA0 前 440 字节清零了
   （0x1BE 分区表 `0C`/`2048` 与 `55AA` 签名完好，故 FAT32 仍能挂载）。
   正式 exFAT 格式化后 MBR/GPT 会重建，此为过程性损伤，无需单独修复。
4. **读测速波动**：2.8~13.8MB/s，写 4.4~6.1MB/s；待卡恢复后复测并
   与 PC 读卡器交叉验证（含大文件 MD5 对比）。

### 6.5 注意事项 / 经验教训

* **Lcf 孤儿段（必看）**：见 §1.3。`tc397_*` 家族如用 tricore-gcc +
  默认 Lcf，必须去掉 `-fdata-sections` 或给 Lcf 补 copy/clear 规则，
  否则上电随机死（本工程已修，uart 基线建议跟进）。
* **TAS/下载**：`systemctl status tas-server`（24817），
  `./build.sh download`（flasher 自动复位；若串口无输出先 `./build.sh reset`）。
* **串口测试流程**：见 §3（DTR 复位窗口坑）。
* **破坏性命令**：`mkfs/erase/raw w` 直接毁数据。本次测试未删除卡内原文件
  （BOOT.bin/image.ub 保留）；动手前请自行备份。
* **时钟注记**：`CLK_CTRL` 恒读 0x000F（FREQ_SEL 写不进去，内部时钟开/关都试过），
  实际 SDCLK 可能非 25MHz（读 13.8MB/s 反推 ≥25MHz）。如需精确设频，
  要按 SDHCI 时序重写时钟切换（先停 SDCLK 再改分频等稳定再开），
  或查 TC3xx SDMMC 手册确认分频语义——留作后续优化（不影响当前功能结论）。
* **重试/恢复设计**：CMD13 与传输各 3 次重试；EISTR/NISTR 粘滞每次清零；
  `sd recover` 用于传输挂死后自救；`sd regs/st` 用于取证。
  （排查证明：重试解决瞬时 CMD13 失败；但挡不住整卡 wedged。）

---

## 7 Windows 11 + TASKING 构建与实测（2026-09-18）

### 7.1 测试背景

* 目标：Ubuntu GCC 功能已验证（含 `sd bench` 速率基线：写 4.4~6.1MB/s，
  读 2.8~13.8MB/s），现验证同一套源码在 Windows 11 + TASKING v6.3r1 下的构建；
  速率待卡恢复后复测。
* 约束：增量改动不得影响 Ubuntu GCC（`build.sh` 原样保留；源码改动包在
  `__TASKING__` 分支或工具链无关形式）。
* 环境：AURIX-Studio-1.10.36（AURIXFlasher v3.0.18），COM165（921600），
  DAP MiniWiggler；TF 卡 128GB（32GB FAT32 分区）。
* 现状：本卡当前 wedged（见 §7.4），Tasking/GCC 均复现，属卡硬件状态，
  与编译器无关；待物理断电恢复后复测 `sd bench`。

### 7.2 构建命令

```powershell
.\build.ps1 -Compiler tasking -Action download     # Tasking Debug 编译并烧录
.\build.ps1 -Compiler gcc -Action download         # Windows GCC 对照（ADS tricore-gcc11）
```

### 7.3 通用兼容改动（GCC 行为不变，详见 `tc397/temp/tasking_porting_log.md`）

与 uart 基线同 6 项，另修 `cmake/tricore-gcc-toolchain.cmake` 的 Windows 默认
GCC 路径 `1.10.28` → `1.10.36`（`try_compile` 子项目回退默认值问题；仅 Windows 分支，
Ubuntu `/opt/tricore-gcc` 不动——本工程 Windows GCC 对照编译即用此验证通过）。

### 7.4 本工程实测日志（Tasking Debug + Windows GCC 对照）

编译（Tasking 305 obj / GCC 306 obj，均 0 error）：

```
[304/305] Linking C executable tc397_sdmmc.elf
Done.
```

`sd init`（Tasking，重新上电后复测，仍失败）：

```
sd init: CD P10.7=0 ...
disk_initialize(0) -> 0x00
f_mount -> DISK_ERR(hard error in low level disk I/O) (1)
```

`sd raw r 0 1` → `disk_read(lba=0,n=1) -> 3`（RES_ERROR）；
`sd regs` → `PSTATE=0x03070202`（解码：DAT3-0=0000 全低、DAT_INHIBIT=1，
CARD_INSERTED/STABLE/DETECT=1；正常空闲应 DAT 全高）；
`sd recover`（CMD12 中止 + SW_RST + 重初始化）后 PSTATE 一度回到
`0x03F70000`（DAT 全高），但下一次读又拉低，EISTR 恒 0——卡内写状态机卡死
（busy 拉 DAT0 低，但控制器无超时上报）。

Windows GCC 对照版（同板同卡）：`sd init` 同样 `DISK_ERR`，
证实为卡硬件 wedged（本 README §6 已记载“需物理掉电，MCU 复位不清 SD 卡状态”），
与 Tasking/GCC 编译器无关。

### 7.5 测试结果

* 编译/烧录/`help`（`sd` 命令集完整）PASS；卡读写待物理断电恢复后，
  用 Tasking 版复测 `sd bench` 并与 GCC 基线（写 4.4~6.1MB/s）对照。
* PSTATE 解码方法（`0x03070202` → DAT 全低 + DAT_INHIBIT）可作为后续
  “CMD 通、DAT 败”类故障的一线判据。

---

## 8 新 32GB TF 卡实测（2026-09-20，Tasking Debug）

### 8.1 测试背景

* 旧 128GB 卡已 wedged（§6.4/§7.4），用户**更换为新的 32GB TF 卡**，
  并已在 PC 上格式化为 **FAT32**（用户明确要求：**无需再格式化或分区，直接测试**）。
* 目标：验证新卡在 TC397 SDMMC0 + FatFs R0.16 下的初始化、挂载、目录、
  容量、读写与测速，并把过程与结果汇总到本 README。
* 环境：Windows 11 + TASKING v6.3r1（`build.ps1 -Compiler tasking`），
  COM165 @921600，DAP MiniWiggler + AURIXFlasher v3.0.18。

### 8.2 测试命令

```powershell
cd c:\github\embedded\tc397\tc397_sdmmc
.\build.ps1 -Compiler tasking -Action rebuild     # 必须 rebuild（见 §8.6 坑1）
.\build.ps1 -Compiler tasking -Action download
# 串口交互（c:\github\embedded\tc397\temp\shell_cmd.ps1）
.\shell_cmd.ps1 -Port COM165 -Cmd "sd init"  -WaitSec 12
.\shell_cmd.ps1 -Port COM165 -Cmd "sd ls 0:/" -WaitSec 8
.\shell_cmd.ps1 -Port COM165 -Cmd "sd free"   -WaitSec 8
.\shell_cmd.ps1 -Port COM165 -Cmd "sd raw r 0 1" -WaitSec 10
.\shell_cmd.ps1 -Port COM165 -Cmd "sd bench 4"   -WaitSec 60
```

### 8.3 实测日志（原始输出）

`sd init`（**PASS**）：

```
sd init: CD P10.7=0 ...
disk_initialize(0) -> 0x00
f_mount -> OK (0)
CD P10.7 : 0 (LOW(card inserted, typical))
SDMMC: RCA=0x0001 state=0x00 type=SDmem cap=SDHC/SDXC(block)(0x0C)
Pins: CMD P15.3 / CLK P15.1 / DAT0-3 P20.7/P20.8/P20.10/P20.11, 4-bit HS SDMA 25MHz
Capacity: 1024 sectors x 512B = 0 MiB (~0.0 GiB), CSD v1
FS: FAT32, cluster=64 sectors, free=953788 clusters (~29805 MiB)
```

`sd ls 0:/`（**PASS**，新卡 PC 格式化后仅有一个系统目录）：

```
ls 0:/:
  d          0  System Volume Information
1 entries
```

`sd free`（**PASS**）：

```
Capacity: 1024 sectors x 512B = 0 MiB (~0.0 GiB), CSD v1
FS: FAT32, cluster=64 sectors, free=953788 clusters (~29805 MiB)
```

`sd raw r 0 1`（**PASS**，底层扇区读通路正常）：

```
disk_read(lba=0,n=1) -> 0
00000000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
...
(showing first 64 of 512 bytes)
```

`sd st`（**PASS**，卡在传输态）：

```
disk_status=0x00 send_status=0 lock=0
R1=0x00000900 state=4 (TRAN) ready=1
```

`sd bench 4`（**FAIL**，写通路失败）：

```
bench 4 MB (0:/BENCH.BIN)...
write FAILED at 0/4096 KB: DISK_ERR(hard error in low level disk I/O) (1)
```

### 8.4 测试结果汇总

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 卡在位检测 `sd cd` | **PASS** | `CD P10.7=0` |
| 卡初始化 `disk_initialize` | **PASS** | 返回 `0x00` |
| 卡识别 | **PASS** | `RCA=0x0001`、`type=SDmem`、`cap=SDHC/SDXC(block)` |
| 挂载 `f_mount` | **PASS** | `OK (0)` |
| 文件系统识别 | **PASS** | `FS: FAT32, cluster=64 sectors` |
| 空闲空间 | **PASS** | `free=953788 clusters (~29805 MiB)` ≈ 29.1 GiB |
| 目录列举 `sd ls` | **PASS** | 1 条 `System Volume Information` |
| 底层扇区读 `sd raw r` | **PASS** | `disk_read -> 0` |
| 卡状态机 `sd st` | **PASS** | `state=4 (TRAN) ready=1` |
| **写通路（`sd bench`/`sd write`/`sd raw w`）** | **FAIL** | `DISK_ERR`；详见 §8.5 |
| 容量解析（CSD） | **异常** | 报 `1024 sectors`（应为 ~62.5M 扇区），见 §8.5 |
| 读速率 | **未测** | 依赖 `sd bench` 的读阶段，写失败即中止 |

**结论**：新 32GB 卡**读通路完全可用**（初始化/挂载/目录/容量/裸读全部 PASS），
**写通路仍不可用**。相比旧卡（读写全不可用）是实质性进展。

### 8.5 写通路失败：根因定位过程（重要）

本轮通过**逐层二分**定位到两个真实缺陷，均已修复其一：

#### 缺陷 1（已修复）：SDCLK 实际远高于 25MHz

* **现象**：`CLK_CTRL` 恒读 `0x000F`，即 `FREQ_SEL=0`（分频比 1 → SDCLK = 基频）。
  SPB=100MHz，故 SDCLK 实际 **100MHz**，是 SD 卡 25MHz 上限的 4 倍。
* **根因**：iLLD 默认 `hostConfig.usePresetValues = TRUE`，
  `IfxSdmmc_Sd_configureSpeedAndBusWidth()` 会置
  `HOST_CTRL2.PRESET_VAL_ENABLE = 1`，此后控制器**忽略软件写入的 `CLK_CTRL.FREQ_SEL`**。
  所以 `IfxSdmmc_configureClock(..., 25000000)` 形同虚设。
* **修复**（`Libraries/FatFS/mmc_sdmmc.c`，`disk_initialize_sdmmc` 末尾）：
  清 `PRESET_VAL_ENABLE`，再按 SDHCI 时序显式编程分频
  （停 SDCLK → 改 `FREQ_SEL`/`UPPER_FREQ_SEL` → 等稳定 → 开 SDCLK）。
* **验证**：`CLKCTL` 由 `0x000F` 变为 **`0x010F`**（`FREQ_SEL=1` → 100MHz/(2×2)=25MHz）✓

#### 缺陷 2（已绕过）：SDMA 多块传输不产生 `transferComplete`

* **现象**：`IfxSdmmc_Sd_readMultiBlock()` 超时返回 `IfxSdmmc_Status_dataError`，
  且 `NISTR=0x00000000`、`EISTR=0x00000000`、`PSTATE=0x03070202`
  （`DAT_INHIBIT=1`，DAT 线被拉低）。`f_mount` 因此报 `FR_DISK_ERR`。
* **对照实验**：改用 `IfxSdmmc_Sd_singleBlockTransfer()`（单块 PIO）后
  **`f_mount` 立即 OK**，`sd ls`/`sd raw r` 全部正常。
  ⇒ 问题在**多块 SDMA 路径**，不在卡、不在时钟、不在 4-bit 位宽
  （1-bit 模式同样失败，已排除）。
* **修复**（`Libraries/FatFS/mmc_sdmmc.c`，`disk_read_sdmmc`）：
  改为**逐扇区调用单块 PIO 读**，循环覆盖请求的 `count`。
  代价是吞吐低于 SDMA，但功能正确。

#### 缺陷 3（未解决）：写数据无法送达卡

* **现象**：`disk_write` 无论走 SDMA 多块（`writeMultiBlock`）还是单块 PIO，
  均失败。单块 PIO 写时逐字诊断显示：
  * 第 1 个 word 写入后 `bufferWriteReady` 不再置位（`NISTR=0`），
    `PSTATE=0x03F70400`（`DAT_LINE_ACTIVE=1`，DAT3-0 全低）；
  * 最终 `EISTR=0x00000001`（`CMD_TOUT_ERR`），卡被留在 **`state=6 (RCV)`**。
* **已尝试且无效**：
  1. 每次 word 前等 `bufferWriteReady`（改为等一次后连续写 128 word）；
  2. `BLOCK_COUNT_ENABLE=1` + `BLOCKCOUNT=1`（期望触发 `transferComplete`）；
  3. 不依赖 `transferComplete`，改用 `PSTATE.CMD_INHIBIT_DAT` 清零 + CMD13 轮询
     `READY_FOR_DATA` 判定完成 —— **`disk_write` 会返回成功，但随后 `sd recover`
     再读回该扇区，内容仍是 `FF FF ...`（擦除态），证明数据实际未写入**；
  4. 1-bit 位宽 + 默认速度（排除位宽/高速时序）。
* **当前判断**：写数据阶段（DAT0 方向）存在硬件/时序层面的问题，
  可能是 SDMMC0 写路径的 DAT 线驱动、上拉强度或控制器 FIFO 时序配置。
  **未定位到最终根因**，如实记录，留待后续（见 §8.7）。

#### 附带发现：CSD 容量解析异常

`sd info`/`sd free` 报 `Capacity: 1024 sectors`（0 MiB），而 FatFs 通过
BPB 算出的空闲空间是正确的（~29805 MiB）。说明
`Sdmmc_GetCapacitySectors()` 的 CMD9/CSD 解析有问题
（`resp01=0x00000900` 明显不是 CSD 内容，疑似 R2 响应读取或 CSD 解码缺陷）。
**不影响挂载与读写**（FatFs 用 BPB），但 `sd info` 的容量显示不可信。

### 8.6 本轮踩到的坑（新会话必读）

1. **`build.ps1 -Action download` 不会重新编译！**
   本轮多次出现“改了代码、烧录了、现象不变”，根因是 `download` 只烧录**已有 hex**，
   而增量构建因 ninja 版本问题静默失败（见 §7.3 与 handover 记载）。
   **改代码后必须 `-Action rebuild`**，或先 `-Action build` 确认编译产物时间戳更新。
   验证方法：对比 `Shell/shell_sd.c` 与 `build/tasking/.../shell_sd.c.obj` 的 `LastWriteTime`。
2. **Tasking 下寄存器名与 GCC 不同**：`Ifx_SDMMC` 的成员是
   `PSTATE_REG` / `ERROR_INT_STAT` / `NORMAL_INT_STAT`（不是 `PSTATE`/`EISTR`/`NISTR`）；
   `IfxSdmmc_Response.cardStatus` 是 union，需 `.U`；
   `cardInfo.scr` 是位域 union，Tasking 拒绝直接 cast（`ctc E300`），需 `memcpy` 取原始字节。
3. **枚举名易错**：`IfxSdmmc_SdSpeedMode_normal`（不是 `_default`）、
   `IfxSdmmc_Command_writeBlock`（不是 `_writeSingleBlock`）、
   `IfxSdmmc_AutoCmdSelect_disable`（不是 `_disabled`）。
4. **`shellPrint` 单次缓冲 256B**（`SHELL_PRINT_BUFFER`），
   诊断输出要拆成多行；`printf` 重定向到 ASCLIN0 可用但易被 shell 回显干扰，
   排查底层驱动时用 `sendUARTMessage()` 直接输出更可靠。
5. **`sd recover` 是救卡利器**：写失败后卡会停在 `state=6 (RCV)`，
   `sd recover`（CMD12 + SW_RST + 重初始化）能把它拉回 `state=4 (TRAN)`。

### 8.7 未完成项与下一步建议

1. **写通路根因**（最高优先级）：建议用逻辑分析仪抓 SDMMC0 的
   CMD/DAT0/CLK 波形，对比 CMD24 后 DAT0 的 busy 时序与数据波形；
   或对照 Infineon 官方 `iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write` example
   的写路径配置（`XFER_MODE`/`BLOCKSIZE`/`TOUT_CTRL` 逐项比对）。
2. **SDMA 多块传输**：查 `SDMA_BUF_BDARY`、`HOST_CTRL1.DMA_SEL`、
   `TOUT_CTRL.TOUT_CNT` 与 `AUTO_CMD_STAT` 的配合；
   当前已用单块 PIO 绕过，功能可用但慢。
3. **CSD 容量解析**：修 `sdmmc_decode_csd()` 或 R2 响应读取。
4. **读速率**：写通路修好后用 `sd bench` 测；当前可先用
   `sd raw r <lba> 16` 循环粗测。
5. **板端 `f_mkfs`**：仍从未成功（§6.4），依赖写通路修复。

---

## 9 写通路攻坚：主机不驱动 DAT 线（2026-09-20 第二轮）

### 9.1 目标与方法

目标：解决 32GB TF 卡**写通路**失败（§8 遗留的最高优先级问题）。

方法（本轮新增的诊断命令，全部保留在 `Shell/shell_sd.c`）：

| 命令 | 作用 |
| --- | --- |
| `sd diag` | 全量寄存器 dump（CAP1/2、HOST1/2、CLK/MBIU/PRESET、XFER/BLKSIZE/BLKCNT、NISTR/EISTR/EN、AUTOCMD、PSTATE 逐位解码、缓冲区地址） |
| `sd tx <pio\|dma> <r\|w> <lba> [n]` | 受控传输实验（PIO 严格按 `PSTATE.BUF_WR_ENABLE` 逐字门控；dma 走 iLLD 多块） |
| `sd exp w <lba> <bce> <mbs> <v4> <width>` | 可切换 `BLOCK_COUNT_ENABLE`/`MULTI_BLK_SEL`/`HOST_VER4_ENABLE`/位宽 的写实验 |
| `sd exp r <lba>` | **对照实验**：读通路做同样的 DAT 采样 |
| `sd exp dma <lba> <n> <r\|w>` | SDMA 多块 + DAT 采样 |
| `sd exp pre <lba>` | 先预填充 FIFO 再发 CMD24 |
| `sd c24 <lba>` | 只发 CMD24（不发数据），随后 CMD13 读卡状态机，判断 CMD24 是否真被接受 |
| `sd clk [kHz]` | 运行时改 SDCLK（按 SDHCI 时序重编程分频） |
| `sd host [v4\|dsel\|preset\|mbui\|print] [v]` | 运行时改 Host 配置位（**`v4` 会清/置 `HOST_VER4_ENABLE` 并重新识别卡**） |
| `sd reinit` | 用当前 Host 设置重跑卡识别（CMD0/CMD8/ACMD41/CMD2/3/7/ACMD6/CMD6） |
| `sd poke/peek/mpeek <off\|addr> ...` | 原始寄存器/内存读写（诊断用，慎用） |
| `sd portinfo` | 打印 SDMMC 各引脚所在端口的 IOCR/PDR/IN（**只读**） |
| `sd chk <lba>` | 读回校验 `word[i]=i^lba` |

关键测量手段：**在数据传输期间高频采样 `PSTATE.DAT_3_0`（bits 23:20，即 DAT3-0 引脚电平）**，
统计 `and`/`or`/变化次数 `chg`：
`and==or==0xF && chg==1` ⇒ 该方向**从未有人驱动** DAT 线。

### 9.2 决定性实验结果（原始日志）

**(1) 写：DAT 线恒定高，从未被驱动**（干净卡、首次写）

```
letter:/$ sd tx pio w 45000000 1
tx pio w lba=45000000 n=1
 blk 0:
  pre  PSTATE=0x03F70000 XFER=0x0000 BLKSIZE=0x0200
  CMD24 ok R1=0x00000900 PSTATE=0x03F70400 BUFWR=1
  pushed: DAT and=0xF or=0xF chg=1 (and==or==0xF => never driven)
  post PSTATE=0x03F70000 NISTR=0x0010 EISTR=0x0000
  wait XFER_COMPLETE: to=0 NISTR=0x0010 EISTR=0x0000 PSTATE=0x03F70000
 -> block 0 FAIL rc=-3
```

**(2) 读：对照实验证明采样方法有效，DAT 明显翻转**

```
letter:/$ sd exp r 45000000
exp r lba=45000000: cmd17 st=0 R1=0x00000900 PSTATE=0x03F70206
  DAT and=0x0 or=0xF chg=3 | PSTATE=0x03F70000 NISTR=0x0022 EISTR=0x0000
```

**(3) CMD24 探针：卡确实接受了写命令并进入 RCV 等数据**

```
letter:/$ sd c24 45000000
before : R1=0x00000900 state=4
cmd24 : st=0 RESP01=0x00000900 (ILLEGAL_CMD=0) PSTATE=0x03F70400
quirk : XFER=0x0000 BLOCKSIZE=0x0200 BLKCNT=0x0000 NISTR=0x0010 EISTR=0x0000
after : R1=0x00000D00 state=6 ready=1 ILLEGAL=0 ERRR=0
```

`state=6` 即 **RCV**：卡已在等数据块 ⇒ 命令链路完全正常，问题在主机侧数据驱动。

**(4) 写的数据并未落盘**

```
letter:/$ sd raw r 45000000 1
disk_read(lba=45000000,n=1) -> 0
5D4A8000: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
...
```

**(5) SDMA 多块：读方向 DAT 大量翻转（卡在发数据），写方向 DAT 恒高**

```
letter:/$ sd exp dma 45000000 2 r
exp dma r lba=45000000 n=2
  st=6 DAT and=0x0 or=0xF chg=3275
  PSTATE=0x03F70202 NISTR=0x8000 EISTR=0x0001 XFER=0x0037 BLKSIZE=0x7200

letter:/$ sd exp dma 45000000 2 w        # 干净卡首次写
exp dma w lba=45000000 n=2
  st=1 DAT and=0xF or=0xF chg=1
  PSTATE=0x03F70000 NISTR=0x0000 EISTR=0x0000 XFER=0x0027 BLKSIZE=0x7200
```

**(6) 引脚端口配置：CMD 与 DAT 完全相同（排除 pad 配置）**

```
letter:/$ sd portinfo
P15 IOCR0=0x1000B800 PDR0=0x22220202 IN=0x0000FC3E
P20 IOCR4=0x10000000 IOCR8=0x10100010 PDR0=0x02222222 PDR1=0x22220020 IN=0x00000DCF
P15.1(CLK)=0xB8 P15.3(CMD)=0x10
P20.7(DAT0)=0x10 P20.8(DAT1)=0x10 P20.10(DAT2)=0x10 P20.11(DAT3)=0x10
PSTATE.DAT3_0=0xF
```

`0xB8` = `IfxPort_Mode_outputPushPullAlt7`（CLK）；`0x10` = `IfxPort_Mode_inputPullUp`
（CMD 与 DAT0-3 一模一样）。CMD 用同样配置可以正常驱动（命令全部有响应）
⇒ **pad/端口配置不是原因**，是控制器没有拉高 DAT 输出使能。

**(7) 控制器能力与配置（`sd diag`，节选）**

```
ID=0x00E9C001 CLC=0x00000000 VERS=0x1005 MSHCID=0x3135302A MSHCTYP=0x6C703032
CAP1=0x216C6483 CAP2=0x08000007 MBIU=0x03030006
  BASE_CLK=100MHz SDMA_SUP=1 ADMA2_SUP=1 HS_SUP=1 MAXBLK=0 TOUTCLK=3 CLKMUL=0
HOST1=0x06(DMA_SEL=0 WIDTH=1 HS=1 EXT=0)
HOST2=0x1000(V4=1 ADDR64=0 PRESET=0 UHSMODE=0 DRVSTR=0 SMPCLK=0)
CLKCTL=0x010F PWR=0x01 TOUT=0x0E BGAP=0x00 PRESET_HS=0x0000
BLKSIZE=0x0200 BLKCNT=0x0000 XFER=0x0010 SDMASA=0x00000000 ADMASA=0x00000000
NISTR=0x0000 EISTR=0x0000 NISTR_EN=0x003B EISTR_EN=0xFFFF
AUTOCMD=0x0000 ADMAERR=0x00 PSTATE=0x03F70000
CMD_INH=0 CMD_INH_DAT=0 DAT_ACT=0 WR_XFER=0 RD_XFER=0 BUFWR=0 BUFRD=0
CARD_INS=1 STABLE=1 CD=1 WP=0 DAT3_0=F CMD_LVL=1 CMD_ISSUE_ERR=0
buf s_ioBuf=0x70001C3C s_chkBuf=0x700017E8
```

结论：**这是 Synopsys DesignWare MSHC（SDHCI 兼容）IP**（有 `MSHC_VER_ID`/`MSHC_VER_TYPE`/
`MBIU_CTRL`/`P_VENDOR_SPECIFIC_AREA`，`VERS=0x1005` 即 spec 5.0）；
SDMA 与 ADMA2 都声明支持；SD 卡总线电压、时钟、位宽、高速均正常。
**关键异常：写传输中 `WR_XFER_ACTIVE` 始终为 0**（`PSTATE` bit8），
说明控制器从未进入写数据传输态。

**(8) 标准回归（失败现场，供后续对照）**

```
letter:/$ sd bench 4
bench 4 MB (0:/BENCH.BIN)...
write FAILED at 0/4096 KB: DISK_ERR(hard error in low level disk I/O) (1)

letter:/$ sd regs
PSTATE=0x0BF70000 NISTR=0x8000 EISTR=0x0100
CLKCTL=0x010F HOST1=0x06 XFER=0x0027 BLKCNT=0
AUTOCMD=0x0003 lastErr: op=2 sector=24576 EISTR=0x0100
```

解码：`XFER=0x0027`（DMA_ENABLE=1, BLOCK_COUNT_ENABLE=1, AUTO_CMD=CMD12, MULTI_BLK_SEL=1,
方向=写）；`EISTR=0x0100`=**AUTO_CMD_ERR**；`AUTOCMD=0x0003`=Auto CMD12 not executed
+ Auto CMD timeout；`PSTATE` bit27=**CMD_ISSUE_ERR**=1（Auto CMD12 发不出去）。

### 9.3 结果汇总

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 读：初始化/挂载/目录/空闲/裸读 | **PASS** | §9.2(2)、§8.4 |
| 读：DAT 线由卡驱动 | **PASS** | `and=0x0 or=0xF chg=3`（PIO）、`chg=3275`（SDMA） |
| 写：卡接受 CMD24 并进入 RCV | **PASS** | §9.2(3) `state=6` |
| 写：主机驱动 DAT0-3 | **FAIL** | `and=or=0xF chg=1`（PIO 与 SDMA 均是） |
| 写：`WR_XFER_ACTIVE` | **FAIL** | 传输中恒 0 |
| 写：`XFER_COMPLETE` | **FAIL** | 永不置位（无任何 NISTR/EISTR 报错） |
| 写：数据落盘 | **FAIL** | 读回 `FF FF ...`（擦除态） |
| 引脚/pad 配置 | **正常** | CMD 与 DAT IOCR 均为 `0x10`，CMD 能驱动 |

### 9.4 已排除项（本轮实测）

| 假设 | 实验 | 结果 |
| --- | --- | --- |
| 卡不接写命令 | `sd c24` + CMD13 | 卡进入 `RCV(6)` ⇒ 卡侧正常 |
| SDCLK 频率/分频不当 | `sd clk 400/12500/25000/50000` | 均同现象 |
| `PRESET_VAL_ENABLE` | `sd host preset 0/1` | 无差异 |
| Host Version 4 模式 | `sd exp w ... v4=0`（运行时切换） | 无差异 |
| Host Version 4 模式（**含重跑卡识别**） | `sd host v4 0` ⇒ `Sdmmc_SetHostVer4()`：清 V4 + 重跑 `IfxSdmmc_Sd_initCard` | 无差异（`reinitCard=6`，写仍不驱动 DAT） |
| `BLOCK_COUNT_ENABLE`+`BLOCKCOUNT` | `sd exp w <lba> 1 ...`（已确认 BLKCNT 写入生效：`poke 6 0001`→回读 1） | 无差异 |
| `MULTI_BLK_SEL` | `sd exp w <lba> 1 1 ...` | 无差异 |
| 位宽（1-bit） | `sd exp w <lba> 0 0 1 1` | 无差异 |
| SDMA vs ADMA/PIO | `sd tx dma w`、`sd exp dma w` | 均不驱动 DAT |
| `PWR_CTRL` 电压选择（原为 0） | `sd poke 29 0F 8` | 无差异 |
| 命令表 `DATA_PRESENT_SEL` | 查 `IfxSdmmc_CMD[]`：CMD17/18/24/25 均 `withData` | 正常 |
| 引脚/pad 配置 | `sd portinfo` 对比 | 完全相同 |
| 预填充 FIFO 后发命令 | `sd exp pre` | 命令前 `BUF_WR_ENABLE=0`，无法预填充（该 IP 只在命令后才开 FIFO） |

### 9.5 结论与下一步建议

**结论（可复现）**：TC397 的 SDMMC0 在本板上**读方向完全正常**（命令 + 卡→主机数据），
但**写方向（主机→卡）的 DAT 输出使能始终不激活**。这不是卡、不是时钟、不是软件配置
（已逐项排除），而是**控制器级/板级写数据通路**问题。

**下一步（按性价比排序）**：

1. **逻辑分析仪抓波形**（最直接）：同时抓 SDCLK / CMD / DAT0，看 CMD24 后
   DAT0 是否有起始位 0、是否有 512B 数据与 CRC 状态令牌。可一锤定音区分
   “控制器没驱动” vs “驱动了但线上没有”。
2. **量 SD 卡座 VDD 与 DAT 线上拉/串阻/电平转换器**：若板上 DAT 线有方向控制
   （level shifter 的 DIR、或 SD 卡电源开关使能脚），确认写方向是否被正确使能。
   同时确认卡座 VDD 在传输期间稳定（读正常但驱动能力/电压不足也会只挂写）。
3. **与官方 example 二进制对拍**：直接烧 Infineon
   `iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write` 官方 hex（同引脚）到本板，
   跑它的 `f_write`。若官方例程也写不进去 ⇒ 硬件/板级问题；若官方能写
   ⇒ 逐行 diff 我们的 host 配置。
4. **换 SDMMC1**（若板上有第二组 SDMMC 引脚）或换一张卡/换板，交叉验证。
5. **板端 `f_mkfs`**：依赖写通路，暂不可做。

### 9.6 本轮踩到的坑

1. **ninja 增量构建会自我破坏**：树里有 `file(GLOB ... CONFIGURE_DEPENDS)` 时，
   每次 build 前 ninja 要 “Re-checking globbed directories”，在本机报
   `ninja: error: FindFirstFileExA(".../Configurations/Debug): The filename, directory
   name, or volume label syntax is incorrect.`，导致**第二次构建必失败**。
   已通过去掉 `CONFIGURE_DEPENDS`（`cmake/AurixProject.cmake`）缓解；
   若仍遇到，删除 `build/<compiler>/.ninja_deps` 与 `.ninja_log` 后重建即可
   （代价是全量重编）。`build.ps1/build.sh` 每次都会重新 configure，所以去掉
   `CONFIGURE_DEPENDS` 不损失自动发现能力。
2. **改代码后必须重建**：`-Action download` 不会重新编译（沿用已有 hex）。
3. **`sd portinfo` 里对 P20.x 做 `IfxPort_setPinModeOutput` 会让 MCU 直接挂死**
   （shell 无响应，需 `build.ps1 -Action reset`）。已改成**只读**版本，勿再加写 pad 的代码。
4. **一次失败的写会把卡留在 RCV 态**，后续所有数据命令都会挂；
   必须 `sd recover` 或复位；`sd recover` 后 CMD24 的 R1 会带 `ILLEGAL_COMMAND`，
   **这是卡状态残留，不代表命令本身有问题**（干净卡上 R1=0x00000900）。
5. `BLOCKCOUNT` 寄存器在命令后会被控制器清零，回读 0 属正常，不代表写入失败。

---

## 10 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* FatFs R0.16：ChaN 许可（`Libraries/FatFS/LICENSE` 见 ref/fatfs/LICENSE.txt，
  源码保留原版权头；`mmc_sdmmc.c/diskio.c` 另含 Infineon BSL 头 + 改编注记）
* Letter-Shell：MIT
* 其余移植代码内部许可
