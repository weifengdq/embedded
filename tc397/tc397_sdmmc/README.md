# tc397_sdmmc — TC397XX SDMMC0 + FatFs R0.16 (exFAT/FAT32) + Letter-Shell

本工程由 `tc397_uart_lettershell` 拷贝而来，在 **Ubuntu 26.04 + tricore-gcc 13.4.1 +
CMake/Ninja** 下构建，目标 **TC397XX 292pin**。在 UART Letter-Shell 基线（ASCLIN0
P14.0/P14.1, 921600-8N1，P13.0 LED）之上，新增 **SDMMC0 SD 卡 + FatFs 文件系统**
完整测试能力，配套 128GB TF 卡验证。

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

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* FatFs R0.16：ChaN 许可（`Libraries/FatFS/LICENSE` 见 ref/fatfs/LICENSE.txt，
  源码保留原版权头；`mmc_sdmmc.c/diskio.c` 另含 Infineon BSL 头 + 改编注记）
* Letter-Shell：MIT
* 其余移植代码内部许可
