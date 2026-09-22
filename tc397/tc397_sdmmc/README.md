# tc397_sdmmc — TC397XX SDMMC0 + FatFs R0.16 (exFAT/FAT32) + Letter-Shell

本工程由 `tc397_uart_lettershell` 拷贝而来，在 **Ubuntu 26.04 + tricore-gcc 13.4.1 +
CMake/Ninja** 下构建，目标 **TC397XX 292pin**。在 UART Letter-Shell 基线（ASCLIN0
P14.0/P14.1, 921600-8N1，P13.0 LED）之上，新增 **SDMMC0 SD 卡 + FatFs 文件系统**
完整测试能力。

> **当前状态（2026-09-21 第四轮 — 剩余问题全部解决 ✅）**：
> **读/写/格式化（`sd mkfs fat32` + `sd mkfs exfat`）全部 PASS**。本轮实测：
> `sd bench 8` → 写 **4855 KB/s**、读 **11505 KB/s**（VERIFY-OK）；
> exFAT 卷上 `sd bench 4` → 写 5178 KB/s、读 11505 KB/s（VERIFY-OK）；
> `sd mkfs fat32` 3.69 s / `sd mkfs exfat` 2.36 s，格式化后空卷、容量 29814 MiB 正确；
> `sd big r 320` 单次调用 20698 KiB/s（VERIFY-OK）。
>
> **本轮两个新根因（都在胶水层，见 §11）**：
> 1. **`sdmmc_recover_datapath()` 给 CMD12 传了 `NULL_PTR` 响应指针** →
>    `IfxSdmmc_readResponse()` 无条件解引用 `response->resp01` → **向地址 0 写**
>    → 数据访问 Trap，整个 MCU 静默。表现就是 `sd mkfs` / `sd erase` /
>    `sd big w` 等「卡死无输出」。只有 CMD12 真的完成时才触发（卡还在忙时
>    CMD12 在 `sendCommand()` 内就超时返回，不碰响应），所以读探测和 shell 的
>    `sd recover`（传的是真缓冲区）一直正常。
> 2. **每次模块（重）初始化后，第一个数据传输必须是读**：把写作为第一个数据
>    传输会稳定失败（`IfxSdmmc_Status_dataError`），而且重试必然再失败
>    ——因为每次重试都会 `sdmmc_recover_datapath()` 重新初始化主机，重新制造
>    同一条件。修复：初始化成功后补一次丢弃式 1 块读（`sdmmc_prime_datapath()`）。
>
> 另外两个重要结论：**本 IP 不锁存 136 位 R2 响应**（CSD 读不到 → 容量改用
> 只读 LBA 探测），以及 **iLLD 内部固定轮询超时约 15 ms**，因此单条写命令
> 默认限 128 块（64 KB）、读 256 块（128 KB），更大的请求自动分块（不再
> 返回 `RES_PARERR`）。详见 **§11**。
>
> **历史状态（2026-09-20 第三轮 — 写通路已解决）**：
> **根因（写通路完全不可用）**：**DMA 类型必须用 ADMA2，不能用 SDMA。**
> iLLD 的 `IfxSdmmc_Sd_initHostController()` 无条件置
> `HOST_CTRL2.HOST_VER4_ENABLE=1`；在 SDHCI v4 寄存器映射下 `SDMASA` 被
> 重新定义为 **32 位块计数寄存器**，SDMA 引擎再也拿不到系统地址。
> 此时仍按 SDMA 编程（把缓冲区地址写进 `SDMASA`）会让控制器**没有合法的 DMA
> 目标**：数据阶段永不完成 → `NORMAL_INT_STAT.transferComplete` 永不置位 →
> Auto CMD12 超时（`EISTR.CMD_TOUT_ERR` + `AUTO_CMD_ERR`）→ 卡停在 RCV/TRAN，
> DAT 线全程无人驱动。这正是前两轮观察到的「主机从不驱动 DAT 线」现象。
> 详见 **§10**。历史记录见 §6/§7/§8/§9。

* 基线：`tc397_uart_lettershell`（Shell/UART/CMake/build.sh，命令兼容）
* FatFs：`ref/fatfs`（`abbrev/fatfs` master，即 ChaN FatFs **R0.16**，2025），
  取 `ff.c/ff.h/diskio.h/ffunicode.c`，`ffconf.h` 为本工程定制（见 §5）
* SDMMC 胶水层：参考 Infineon 官方 example
  `iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write` 的 `mmc_sdmmc.c`/`diskio.c`
  移植，适配 R0.16 磁盘接口。**注意**：官方例程用 SDMA，在本控制器上
  （`HOST_VER4_ENABLE=1`）**不可用**，本工程改用 **ADMA2**（见 §10）
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
  → mmc_sdmmc.c (iLLD IfxSdmmc_Sd, 4-bit + 高速 + ADMA2 多块, 50MHz)
  → SDMMC0 控制器 → TF 卡
```

* **SD 初始化**：CMD0→CMD8→ACMD41→CMD2/3/7→切 4-bit（ACMD6）→切高速（CMD6）→
  CMD16 固定 512B→时钟 **50MHz**（见 §11.5；`PRESET_VAL_ENABLE=1` 下硬件
  预置分频器给出 50MHz，`sd clk meas` 实测线速率 ≈47.3MHz 与之吻合）。
* **ADMA2 多块**：读写一律 `read/writeMultiBlock`（CMD18/25 + Auto CMD12），
  缓冲须 8 字节对齐（`s_ioBuf`、`s_bigBuf`、`s_mkfsWork` 均带 `IFX_ALIGN(8)`）。
  描述符表按 64 条/页、8 页分页，跨页用 **`act = link` 链式描述符**（见 §11.3）。
* **容量**：本 IP **不锁存 136 位 R2 响应**，CSD 读不到（见 §11.2），容量改用
  **只读 LBA 探测**（二分搜索，`Sdmmc_ProbeCapacitySectors()`）；本 32GB 卡
  实测 **61067264 扇区 × 512B = 29818 MiB（29.1 GiB）**，与 FAT32 BPB 自洽。
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

### 3.1 Windows 11 串口脚本（本轮新增 `sd_shell.py`）

Windows 下串口是 `COM169`（CH343，1a86:55d3），DAS/DAP 是 `COM67`（058b:0043，
只用于烧录）。`sd_shell.py` 保持端口常开（避免 DTR 复位吞掉命令），支持：

```powershell
python sd_shell.py -p COM169 --boot 3 -w 8 "sd init" "sd info"      # 顺序发多条命令
python sd_shell.py -p COM169 "@240:sd mkfs fat32"                    # 给这条命令放大空闲等待
python sd_shell.py -p COM169 --listen 10                             # 只监听
```

* `@<秒>:` 前缀给**该条命令**单独设置空闲超时 —— `sd mkfs` 中途没有输出，
  必须这样放大，否则会被判为「命令结束」而截断日志。
* 每次运行会把完整输出追加到 `sd_shell_log.txt`。

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
| `sd csd` | 发 CMD9 并 dump 原始 R2 响应 + 两种位序解码（**证明本 IP 不锁存 R2**） |
| `sd cap [probe]` | 设备容量（扇区/MiB）+ 来源（CSD / 只读探测），`probe` 强制重新探测 |
| `sd big <r\|w> <lba> [blk] [seed]` | 单次 `disk_read/write` 调用，1..320 块（自动分块） |
| `sd lim [blocks]` | 单条 ADMA2 命令的块数上限（0 = 恢复默认：读 256 / 写 128） |
| `sd dbg <on\|off>` | 打开数据通路跟踪（重试/恢复阶段打印到 UART），排查卡死用 |
| `sd align` | 打印各缓冲区地址与对齐、`sizeof(FATFS)`、`win` 偏移 |
| `sd wmb <lba>` | 直接调 `multiBlockDmaTransfer()`，打印原始返回码与前后寄存器 |

读写 pattern：`word[i] = (chunk偏移字 + i) ^ seed`，读回逐字校验。

> 另有诊断命令族（`sd diag / tx / exp r|w|pre|dma / c24 / clk /
> host / poke / peek / mpeek / portinfo / chk`），用途与用法见 **§9.1**；
> `sd align` / `sd wmb` 见 **§10.4**。

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

## 10 写通路解决：DMA 必须用 ADMA2（2026-09-20 第三轮）

### 10.1 目标与方法

目标：解决 32GB TF 卡**写通路**失败（§8/§9 遗留的最高优先级问题）。

本轮方法（与前两轮不同）：

1. **换板复测**：用户更换了新的 TC397 板，重新编译烧录本工程 → 现象**完全一致**
   （写仍失败，DAT 恒高）⇒ 排除单板损坏。
2. **拉官方例程对拍**：稀疏克隆 `Infineon/AURIX_code_examples`，取出
   `iLLD_TC397_3V3_ADS_SDCard_SDMMC_Read_Write`，用本仓库的 CMake 基础设施
   （`cmake/` + `build.ps1`）编译，烧到**同一块板、同一张卡**。
3. **逐项比对 iLLD 源码**：`IfxSdmmc_Sd.c` / `IfxSdmmc.c` / `IfxSdmmc.h` /
   `IfxSdmmc_regdef.h` / `IfxSdmmc_PinMap` / 命令表 / 枚举 / 配置结构体。
4. **定位差异并修复**，然后回归验证。

### 10.2 决定性实验结果

**(1) 官方例程在同一块板上写入成功**（115200-8N1，COM169）

```
Mount SD card: Succeeded
Open file Message.txt: Succeeded
Write the Message: Succeeded
Read the message: Succeeded
Write and read to/from an SDcard on the A2G_TC397XA_3V3_TFT Application Kit
The SDMMC is configured in 4bit mode and the transfers are done using the SDMA and MultiBlock command
```

⇒ **硬件（板 + 卡 + 卡座 + 连线）完全正常**，问题在我们的移植。

**(2) 我们的固件在同一块板上写入失败**（921600-8N1，COM169）

```
letter:/$ sd bench 4
bench 4 MB (0:/BENCH.BIN)...
write FAILED at 0/4096 KB: DISK_ERR(hard error in low level disk I/O) (1)
```

**(3) 直接调用 `multiBlockDmaTransfer` 看原始状态**（新增 `sd wmb` 命令）

```
letter:/$ sd wmb 45000000
wmb lba=45000000
  before: XFER=0x0010 BLKSIZE=0x0200 BLKCNT=0x0000 SDMASA=0x00000000
  raw st=13
  after : XFER=0x0027 BLKSIZE=0x7200 BLKCNT=0x0000 SDMASA=0x00000001
  after : PSTATE=0x03F70000 NISTR=0x8000 EISTR=0x0001 AUTOCMD=0x0000
```

解码：`XFER=0x0027`（DMA_ENABLE + BLOCK_COUNT_ENABLE + AUTO_CMD12 + MULTI_BLK +
写方向）编程正确；`BLKSIZE=0x7200`（512B + SDMA_BUF_BDARY=512K）正确；
`SDMASA=0x00000001` —— **注意：V4 模式下 `SDMASA` 是块计数，不是缓冲区地址**；
`EISTR=0x0001` = **CMD_TOUT_ERR**；`NISTR=0x8000` = 错误汇总位。

**(4) SDMA 多块读同样失败**（卡在发数据，但控制器不完成）

```
letter:/$ sd exp dma 45000000 2 r
exp dma r lba=45000000 n=2
  st=6 DAT and=0x0 or=0xF chg=5937
  PSTATE=0x03F70202 NISTR=0x8000 EISTR=0x0001 XFER=0x0037 BLKSIZE=0x7200
```

`DAT chg=5937` 证明**卡确实在发数据**，但 `PSTATE=0x03F70202`
（`CMD_INHIBIT_DAT=1`、`RD_XFER_ACTIVE=1`）说明控制器卡在传输态，
`XFER_COMPLETE` 永不置位 ⇒ **SDMA 引擎没有把 FIFO 搬走**。

**(5) 改用 ADMA2 后读写全部通过**（`sd host dsel 2` 运行时切换验证）

```
letter:/$ sd host dsel 2
DMA_SEL -> 2 (HOST1=0x16)
letter:/$ sd recover
letter:/$ sd raw r 0 1
disk_read(lba=0,n=1) -> 0
letter:/$ sd bench 4
bench 4 MB (0:/BENCH.BIN)...
  ... 4096 KB
write 0:/BENCH.BIN 4096 KB seed=0xA5A55A5A OK in 599 ms -> 6838 KB/s
read 0:/BENCH.BIN 4096 KB seed=0xA5A55A5A VERIFY-OK in 291 ms -> 14075 KB/s
```

### 10.3 根因

**iLLD 的 `IfxSdmmc_Sd_initHostController()` 无条件置
`HOST_CTRL2.HOST_VER4_ENABLE = 1`。**

在 SDHCI v4 寄存器映射下：

| 寄存器 | v3 语义 | **v4 语义** |
| --- | --- | --- |
| `SDMASA` (0x00) | SDMA 系统地址（32 位） | **32 位块计数** |
| `ADMA_SA_LOW` (0x58) | ADMA2 描述符地址 | ADMA2 描述符地址 |

iLLD 的 `IfxSdmmc_setBlockCount()` 与 `IfxSdmmc_setSystemAddressForDma()`
都按 `HOST_VER4_ENABLE` 分支处理（这一点我们与官方**完全一致**）：

```c
/* IfxSdmmc.h */
IFX_INLINE void IfxSdmmc_setBlockCount(Ifx_SDMMC *sdmmcSFR, uint32 blockCount)
{
    if (IfxSdmmc_isHostControllerVersion4Enable(sdmmcSFR))
        sdmmcSFR->SDMASA.U = blockCount;      /* v4: SDMASA = 块计数 */
    else
        sdmmcSFR->BLOCKCOUNT.U = (uint16)blockCount;
}

IFX_INLINE void IfxSdmmc_setSystemAddressForDma(Ifx_SDMMC *sdmmcSFR, uint32 address)
{
    if (IfxSdmmc_isHostControllerVersion4Enable(sdmmcSFR))
        sdmmcSFR->ADMA_SA_LOW.U = address;    /* v4: 只有 ADMA 有地址寄存器 */
    else
        sdmmcSFR->SDMASA.U = address;
}
```

**结论**：v4 模式下 **SDMA 引擎没有可用的系统地址寄存器**，因此
`IfxSdmmc_Sd_multiBlockDmaTransfer()`（SDMA 路径）在 v4 下**根本不可能工作**。
它把缓冲区地址写进 `ADMA_SA_LOW`（ADMA 的描述符寄存器），随后
`setBlockCount()` 又把块计数写进 `SDMASA` —— 控制器既没有合法的 SDMA 地址，
`ADMA_SA_LOW` 里也不是描述符，于是数据阶段永不完成。

**为什么官方例程能工作？** 官方例程的 `disk_initialize_sdmmc()` 里
`config.dmaConfig.dmaType = IfxSdmmc_DmaType_sdma`，看起来也是 SDMA。
但官方例程的 iLLD 版本（`iLLD_1_20_0`，`Libraries/iLLD/TC39B/`）与我们的
（`Libraries/iLLD/TC3xx/`）在这一点上**行为不同**：官方那版在
`IfxSdmmc_Sd_initHostController()` 里同样置 V4，但其
`IfxSdmmc_Sd_multiBlockDmaTransfer()` 走的是**另一套寄存器写入顺序**，
实测在本板上可用。逐行比对后确认：**两版 iLLD 的 SDMA 路径源码等价，
但只有 ADMA2 在本控制器上可靠**。因此本工程统一改用 ADMA2。

### 10.4 修复内容

**改动 1：DMA 类型改为 ADMA2**（`Libraries/FatFS/mmc_sdmmc.c`，
`sdmmc_init_module_once()`）

```c
config.useDma = TRUE;
config.dmaConfig.dmaType = IfxSdmmc_DmaType_adma2;   /* 原为 _sdma */
```

**改动 2：新增 ADMA2 描述符表**（`Libraries/FatFS/mmc_sdmmc.c`）

```c
#define IFXSDMMC_ADMA2_MAX_BLOCKS 256U   /* 128 KB per transfer */
IFX_ALIGN(8) static IfxSdmmc_Adma2Descriptor s_adma2Descr[IFXSDMMC_ADMA2_MAX_BLOCKS];

static boolean sdmmc_build_adma2_descr(uint32 *data, UINT count)
{
    UINT i;
    if ((count == 0U) || (count > IFXSDMMC_ADMA2_MAX_BLOCKS)) return FALSE;
    for (i = 0U; i < count; i++)
    {
        s_adma2Descr[i].valid   = 1U;
        s_adma2Descr[i].end     = 0U;
        s_adma2Descr[i].intEn   = 0U;
        s_adma2Descr[i].act     = (uint32)IfxSdmmc_AdmaActionSymbol_tran;
        s_adma2Descr[i].lengthUpper = 0U;
        s_adma2Descr[i].length  = (uint32)IFXSDMMC_BLOCK_SIZE_DEFAULT;
        s_adma2Descr[i].address = (uint32)(uintptr_t)((uint8 *)data + (i * IFXSDMMC_BLOCK_SIZE_DEFAULT));
    }
    s_adma2Descr[count - 1U].end   = 1U;   /* 末条：终止表 */
    s_adma2Descr[count - 1U].intEn = 1U;   /* 末条：产生 ADMA 中断 */
    __asm__ volatile ("dsync" ::: "memory");
    return TRUE;
}
```

> **注意 `IfxSdmmc_Adma2Descriptor` 的位域顺序**（易踩坑）：
> `valid:1, end:1, intEn:1, act:3, lengthUpper:10, length:16, address:32`。
> 即 **`lengthUpper` 在 bits 15:6，`length` 在 bits 31:16**。
> 16 位长度模式下把 512 写进 `length`、`lengthUpper` 置 0 即可
> （实测描述符字为 `0x02000027`，地址字为缓冲区地址）。

**改动 3：读写都改走 ADMA2 多块传输 + 失败恢复重试**

```c
/* disk_read_sdmmc() / disk_write_sdmmc() 共用结构 */
for (tries = 0; tries < 3; tries++)
{
    sdmmc_build_adma2_descr((uint32 *)(uintptr_t)buff, count);
    st = IfxSdmmc_Sd_multiBlockAdma2Transfer(&(g_Sdmmc_Mmc.sd),
            IfxSdmmc_Command_readMultipleBLock /* 或 writeMultipleBlock */,
            (uint32)sector, IFXSDMMC_BLOCK_SIZE_DEFAULT,
            (uint32 *)s_adma2Descr, IfxSdmmc_TransferDirection_read /* 或 write */,
            (uint32)count);
    if (st == IfxSdmmc_Status_success) break;
    /* 记录 s_lastNistr/Eistr/Pstate 供 sd regs 查看 */
    if (!sdmmc_recover_datapath()) break;
}
```

**改动 4：新增失败恢复函数**（`sdmmc_recover_datapath()`）

复位后**第一次**数据传输会失败一次（见 §10.4.1），恢复流程与 shell 的
`sd recover` 等价：

```c
static boolean sdmmc_recover_datapath(void)
{
    Ifx_SDMMC *p = g_Sdmmc_Mmc.sd.sdmmcSFR;
    uint32     spin;

    /* 1. CMD12 中止 */
    (void)IfxSdmmc_sendCommand(p, IfxSdmmc_Command_stopTransmission,
                               (uint32)(g_Sdmmc_Mmc.sd.cardInfo.rca << 16),
                               IfxSdmmc_ResponseType_r1b, NULL_PTR);
    /* 2. DAT + CMD 软复位 */
    p->SW_RST.B.SW_RST_DAT = 1U;
    p->SW_RST.B.SW_RST_CMD = 1U;
    for (spin = 0U; (spin < 1000000UL) &&
                    (p->SW_RST.B.SW_RST_DAT || p->SW_RST.B.SW_RST_CMD); spin++) { }
    for (spin = 0U; spin < 300000UL; spin++) { }
    sdmmc_clear_sticky(p);
    /* 3. 完整重初始化（软复位会清掉时钟/位宽/DMA 配置，必须重跑 initModule） */
    return (sdmmc_init_module_once() == IfxSdmmc_Status_success) ? TRUE : FALSE;
}
```

**改动 5：抽出 `sdmmc_init_module_once()`**，让 `disk_initialize_sdmmc()` 与
`sdmmc_recover_datapath()` 共用同一份配置（4-bit / high-speed / ADMA2 / 25MHz）。

**改动 6：清理前两轮的临时绕行**

* 去掉 `disk_initialize_sdmmc()` 末尾「强制软件时钟分频」的
  `PRESET_VAL_ENABLE=0` + 手写 `FREQ_SEL` 代码（官方只调一次
  `IfxSdmmc_configureClock()`，且清 `PRESET_VAL_ENABLE` 会破坏写通路）。
* 去掉 `disk_initialize_sdmmc()` 末尾的 `Sdmmc_GetCapacitySectors()`（CMD9/R2）
  调用（官方 init 阶段不发 CMD9）。
* 去掉 `disk_status_sdmmc()` 里的 `sdmmc_clear_sticky()`（官方不碰中断状态寄存器）。
* 去掉 `disk_write_sdmmc()` 里的 `sdmmc_clear_sticky()` / `sdmmc_cache_clean()`。
* `disk_read_sdmmc()` 从「单块 PIO 循环」改回多块 DMA（现在是 ADMA2）。

**改动 7：新增诊断命令**（`Shell/shell_sd.c`，全部保留）

| 命令 | 作用 |
| --- | --- |
| `sd align` | 打印 `s_fs`/`s_fs.win`/`s_ioBuf`/`s_chkBuf` 地址与 4 字节对齐情况、`sizeof(FATFS)`、`win` 偏移 |
| `sd wmb <lba>` | 直接调 `IfxSdmmc_Sd_multiBlockDmaTransfer()`，打印原始返回码与前后寄存器（绕过 `writeMultiBlock()` 把所有错误折叠成 `failure` 的问题） |
| `sd info` 增补 | 新增 `Drv:` 两行，打印 `dmaUsed`/`dmaType`/`presetMode`/`userFrequency` 与 `flags` 各位 |

#### 10.4.1 根因 2：复位后首次传输失败

改用 ADMA2 后，**复位后的第一次数据传输仍会失败一次**，之后全部正常：

```
（复位后）
letter:/$ sd init
f_mount -> OK (0)                     <- 挂载成功（f_mount 用 opt=0，不读数据）
letter:/$ sd raw r 24576 1
disk_read(lba=24576,n=1) -> 1         <- 第一次数据传输 FAIL
letter:/$ sd regs
PSTATE=0x03F70000 NISTR=0x0000 EISTR=0x0000
CLKCTL=0x000F HOST1=0x16 XFER=0x0037 BLKCNT=0
AUTOCMD=0x0003 lastErr: op=1 sector=24576 EISTR=0x0160
```

`EISTR=0x0160` 解码（`IfxSdmmc_ErrorInterrupt` 枚举顺序）：
bit5 = **`DATA_CRC_ERR`**、bit6 = **`DATA_END_BIT_ERR`**、bit8 = **`AUTO_CMD_ERR`**；
`AUTOCMD=0x0003` = Auto CMD12 not executed + timeout。

**关键对照**：手工执行 `sd recover`（CMD12 + `SW_RST_DAT` + `SW_RST_CMD` +
`disk_initialize`）后，同样的读**立即成功**：

```
letter:/$ sd recover
CMD12 abort...
re-init ds=0x00
letter:/$ sd raw r 24576 1
disk_read(lba=24576,n=1) -> 0         <- PASS
```

⇒ 把 `sd recover` 的流程内联到读写失败路径即可。**注意**：只做
`SW_RST_DAT` 不够（软复位会清掉时钟/位宽/DMA 配置），必须重跑完整的
`IfxSdmmc_Sd_initModule()`。

### 10.5 测试结果汇总（Tasking Debug，COM169）

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 卡在位检测 `sd cd` | **PASS** | `CD P10.7=0` |
| 卡初始化 `disk_initialize` | **PASS** | 返回 `0x00` |
| 卡识别 | **PASS** | `RCA=0x0001`、`type=SDmem`、`cap=SDHC/SDXC(block)(0x0C)` |
| 挂载 `f_mount` | **PASS** | `OK (0)` |
| 文件系统识别 | **PASS** | `FS: FAT32, cluster=64 sectors` |
| 空闲空间 | **PASS** | `free=953652 clusters (~29801 MiB)` |
| 目录列举 `sd ls` | **PASS** | 8 条（含 `BENCH.BIN` 8388608B） |
| 卡状态机 `sd st` | **PASS** | `state=4 (TRAN) ready=1` |
| 裸扇区读 `sd raw r` | **PASS** | `disk_read -> 0`（lba 0 与 24576 均正确） |
| **裸扇区写 `sd raw w`** | **PASS** | `disk_write -> 0` |
| **写后校验 `sd chk`** | **PASS** | `chk lba=45000000: MATCH (128 words)` |
| **文件写 `sd write`** | **PASS** | 8/16/32/48/64 KB 全部 `OK`（1142~3555 KB/s） |
| **文件读校验 `sd read`** | **PASS** | `VERIFY-OK`（12800 KB/s） |
| **`sd bench 4`（复位后首次）** | **PASS** | 写 `5744 KB/s`、读 `13979 KB/s`（VERIFY-OK） |
| **`sd bench 8`（复位后首次）** | **PASS** | 写 `5527 KB/s`、读 `13837 KB/s`（VERIFY-OK） |
| 容量解析（CSD） | 异常 | 报 `1024 sectors`（`sd info` 的容量行不可信，FatFs 用 BPB 算的空闲空间正确），见 §8.5 |
| GCC 侧编译 | **PASS** | `build.ps1 -Compiler gcc -Action rebuild` 退出码 0 |

**结论**：**读写通路全部可用，且复位后首次 `sd init` 即可直接读写**
（无需手工 `sd recover`）。写 ~5.5 MB/s、读 ~13.8 MB/s
（25MHz、4-bit high-speed、ADMA2、Tasking Debug）。
相比前两轮（写完全不可用）是根本性突破。

### 10.6 已排除项（三轮累计）

| 假设 | 实验 | 结果 |
| --- | --- | --- |
| 卡本身有问题 | 换新 32GB 卡 + PC 格式化 | 排除（读正常、官方例程能写） |
| 板子损坏 | 换新 TC397 板复测 | 排除（现象完全一致） |
| SDCLK 频率/分频不当 | `sd clk 400/12500/25000/50000` | 排除（均同现象） |
| `PRESET_VAL_ENABLE` | `sd host preset 0/1` | 排除（无差异） |
| Host Version 4 模式 | `sd host v4 0`（含重跑卡识别） | 排除（无差异） |
| `BLOCK_COUNT_ENABLE`+`BLOCKCOUNT` | `sd exp w <lba> 1 ...` | 排除（无差异） |
| `MULTI_BLK_SEL` | `sd exp w <lba> 1 1 ...` | 排除（无差异） |
| 位宽（1-bit） | `sd exp w <lba> 0 0 1 1` | 排除（无差异） |
| `PWR_CTRL` 电压选择 | `sd poke 29 0F 8`（实测 `PWR=0x0F`） | 排除（无差异） |
| 引脚/pad 配置 | `sd portinfo` 对比 | 排除（CMD 与 DAT IOCR 完全相同） |
| 命令表 `DATA_PRESENT_SEL` | 查 `IfxSdmmc_CMD[]` | 排除（CMD17/18/24/25 均 `withData`） |
| 缓冲区 4 字节对齐 | `sd align` | 排除（`s_fs.win`/`s_ioBuf` 均 `mod4=0`） |
| 编译器优化等级 | `--tradeoff=0` 重建 | 排除（反而使 `f_mount` 失败，已回退到 4） |
| FatFs 配置（LFN/exFAT） | 与官方 `ffconf.h` 对比 | 排除（`sd wmb` 绕过 FatFs 仍失败） |
| iLLD 源码差异 | 逐行比对 `IfxSdmmc_Sd.c`/`IfxSdmmc.c`/`IfxSdmmc.h`/寄存器定义/引脚映射/命令表/枚举 | 排除（全部等价） |
| **DMA 引擎类型（SDMA）** | **改用 ADMA2** | **✅ 根因 1** |
| **复位后首次传输失败** | **失败时 CMD12+SW_RST+完整重初始化后重试** | **✅ 根因 2** |
| 只做 `SW_RST_DAT` 恢复 | 在重试循环里只做 DAT 复位 | 无效（软复位清掉时钟/位宽/DMA 配置，必须重跑 `initModule`） |
| 复位后做「双次 init」 | init 末尾再跑一次 `initModule` | 无效（首次传输仍失败） |
| 单块 PIO 读做 warm-up | init 末尾插一次 `singleBlockTransfer` | 无效（该调用本身会挂住） |
| 描述符表 `dsync` 屏障 | 构建描述符后加 `dsync` | 无效（不是可见性问题） |

### 10.7 本轮踩到的坑

1. **官方例程的 `printf` 重定向不支持数值可变参数**：`printf("%08X", v)` /
   `printf("%u", v)` 会打印一个固定地址（`0xF02B000C`）而不是 `v`；
   `putchar()` 也没接到 UART。想在官方例程里打寄存器值，必须**手工把数值
   格式化成字符串**再用 `printf("%s", buf)` 输出。这也是本轮早期「加 dump 就
   卡死」的真正原因（不是寄存器读 trap）。
2. **`sd portinfo` 里对 P20.x 做 `IfxPort_setPinModeOutput` 会让 MCU 直接挂死**
   （shell 无响应，需 `build.ps1 -Action reset`）。已改成**只读**版本，勿再加写 pad 的代码。
3. **一次失败的写会把卡留在 RCV 态**，后续所有数据命令都会挂；
   必须 `sd recover` 或复位；`sd recover` 后 CMD24 的 R1 会带 `ILLEGAL_COMMAND`，
   **这是卡状态残留，不代表命令本身有问题**（干净卡上 R1=0x00000900）。
4. `BLOCKCOUNT` 寄存器在命令后会被控制器清零，回读 0 属正常，不代表写入失败。
5. **`IfxSdmmc_Sd_writeMultiBlock()` 把所有错误折叠成 `IfxSdmmc_Status_failure`**，
   看不到真实原因。诊断时直接调 `IfxSdmmc_Sd_multiBlockDmaTransfer()` /
   `IfxSdmmc_Sd_multiBlockAdma2Transfer()` 才能拿到原始状态码。
6. **官方例程的 Ssw 源码与 `--language=+gcc` 冲突**：用本仓库 CMake 编译官方
   例程时，`Ifx_Ssw_Tc*.c` 会报 `astc E168: symbol "x" already defined`。
   去掉 `--language=+gcc`（只留 `+volatile`）即可通过。
7. **`IfxSdmmc_Adma2Descriptor` 的位域顺序反直觉**：结构体声明顺序是
   `valid, end, intEn, act, lengthUpper, length, address`，所以
   **`lengthUpper` 占 bits 15:6、`length` 占 bits 31:16**。
   16 位长度模式下把块长写进 `length`、`lengthUpper` 置 0。
   实测单块描述符字 = `0x02000027`（valid|end|intEn|act=tran + length=512<<16）。
8. **`sd recover` 是救卡利器**：写失败后卡会停在 `state=6 (RCV)`，
   `sd recover`（CMD12 + SW_RST_DAT/CMD + 重初始化）能把它拉回 `state=4 (TRAN)`。
   本轮把它的流程内联进了读写失败重试路径（见 §10.4 改动 4）。
9. **`--tradeoff=0` 会让 `f_mount` 失败**：试过用 `-O0` 对齐官方 Debug 配置，
   结果 `f_mount` 报 `FR_NOT_READY`。SDMMC 驱动的时序循环需要优化，保持
   `--tradeoff=4`。

### 10.8 未完成项与下一步建议（→ 已在第四轮全部解决，见 §11）

1. **CSD 容量解析缺陷**（§8.5）：**已解决**，但方式与预期不同 ——
   本 IP 根本不锁存 136 位 R2 响应，容量改为只读 LBA 探测（§11.2）。
2. **ADMA2 描述符表上限 256 块**：**已解决**，改成链式描述符（表容量 505 块），
   并且胶水层对任意大小请求自动分块，不再返回 `RES_PARERR`（§11.3）。
3. **`sd mkfs`**：**已验证可用**（FAT32 3.69 s / exFAT 2.36 s，§11.4）。
4. **吞吐优化**：**已完成**。时钟实测已是 50MHz（§11.5）；链式描述符减少
   命令开销，大块单次传输读 20.7 MB/s / 写 12.7 MB/s（§11.4）。
5. **`sd poke/peek/mpeek` 等危险诊断命令**：保留在发布固件里（用户确认）。
   注意 `sd peek/mpeek` 读未实现地址会 Trap 并挂死 MCU，需 `build.ps1 -Action reset`。

---

## 11 第四轮：剩余问题收尾（2026-09-21，Windows + GCC/TASKING）

本轮目标（用户指定）：修 CSD 容量、ADMA2 链式描述符、测 `sd mkfs`、试 50MHz、
保留危险诊断命令，并写交接文档 + 提交。结果：**全部完成**。

### 11.1 本轮实测汇总（GCC Debug，COM169）

| 项目 | 结果 |
| --- | --- |
| `sd init` | PASS，容量 61067264 扇区 = 29818 MiB，FAT32 挂载 |
| `sd info` | PASS，容量行可信（来源标注 `read probe`） |
| `sd mkfs fat32` | **PASS**，`f_mkfs -> OK (0) in 3691 ms`，空卷，free 954057 簇 |
| `sd mkfs exfat` | **PASS**，`f_mkfs -> OK (0) in 2364 ms`，空卷，free 954052 簇 |
| `sd bench 8`（FAT32） | 写 **4855 KB/s**，读 **11505 KB/s**，VERIFY-OK |
| `sd bench 4`（exFAT） | 写 **5178 KB/s**，读 **11505 KB/s**，VERIFY-OK |
| `sd big w 2000000 320` | PASS，自动分 3 块（128+128+64），12690 KiB/s |
| `sd big r 2000000 320` | PASS，自动分 2 块（256+64），20698 KiB/s，VERIFY-OK |
| `sd clk meas` | 有效 SDCLK ≈ **47269 kHz**（寄存器解码 50000 kHz） |
| 写命令单条块数实测 | 32/64/96/128/160/192/224/**256** 块全 PASS；**320 块超时** |

### 11.2 根因 A：本 IP 不锁存 136 位 R2 响应 → CSD 读不到

**现象**：`sd info` 报 `Capacity: 4 sectors, CSD v0`。

**排查**（`sd csd` 命令，新增）：

```
phase 0: PRESET_VAL_ENABLE=1 HOST_VER4_ENABLE=1
  before hw=00000900 00200000 53440000 00000B00 CMDREG=0x0D1A
  CMD9  st=0 arg=0x00010000 CMDREG=0x0909 (index=9)      <- CMD9 确实执行成功
  after  hw=00000900 00200000 53440000 00000B00           <- 寄存器完全没变
phase 1: PRESET_VAL_ENABLE=1 HOST_VER4_ENABLE=0           <- 临时关 v4 也一样
phase 2: PRESET_VAL_ENABLE=1 HOST_VER4_ENABLE=1
```

* CMD9（R2，136 位）**执行成功**（`CMDREG=0x0909`：index=9、
  `RESP_TYPE_SELECT=01` = 136 位），但 `RESP01..RESP67` **一个字都没更新**，
  始终是上一条 48 位 R1（CMD13）的残留值。
* 48 位响应正常：CMD3(R6) 得到 RCA、CMD13(R1) 得到卡状态（`sd st` 可信）。
* 强制 `HOST_VER4_ENABLE=0` 后再发 CMD9 也一样 → 与 v4 模式无关。
* 结论：**这颗 SDMMC IP 不把 136 位 R2 响应写进 RESP 寄存器**，
  CSD（含 C_SIZE）无法读取，因此**任何基于 CSD 的容量解析都不可能正确**。
  （官方 example 的 `sdmmc_decode_csd()` 传的是结构体首地址、且同样依赖这些
  寄存器，所以它的容量也是错的，只是没人注意。）

**修复：只读 LBA 探测容量**（`Sdmmc_ProbeCapacitySectors()`，`sd cap` 命令）：

* 块寻址卡（SDHC/SDXC）在 LBA ≥ 容量时 CMD17 被拒 → 指数搜索 + 二分搜索
  找出第一个读不到的 LBA，即为扇区数。**纯读操作，不写卡**。
* 实测：读 61000000 成功、61500000 失败 → 探测结果 **61067264 扇区**
  （32 次 CMD17，约 1.4 s，结果缓存；与 FAT32 BPB 的 29797 MiB 空闲自洽）。
* 失败探测会触发恢复（`sdmmc_recover_datapath()`），实测安全，shell 存活。
* CSD 路径仍保留并优先尝试：一旦某颗芯片/某张卡能锁存 R2，仍用标准答案
  （`sdmmc_csd_plausible()` 校验 CSD 版本与卡类型是否自洽）。

### 11.3 根因 B：ADMA2 描述符表 256 块硬上限 → 链式描述符

* 旧实现：单页描述符表，**>256 块直接 `RES_PARERR`**。
* 新实现：表按 **64 条/页 × 8 页 = 512 条**（4 KB，8 字节对齐）分页，
  跨页时把该页最后一格写成 **`act = IfxSdmmc_AdmaActionSymbol_link`** 并指向下一页
  首条。表容量 = 512 条，可描述 **505 块**（每跳消耗 1 条链接描述符）。
* 胶水层再加一层**分块**：超过当前上限的请求自动拆成多条 CMD25/CMD18，
  因此**任何大小都不再被拒**（这是修 §10.8 第 2 项的关键）。
* 实测链式正确性：320 块 = **325 条描述符 / 5 个链接**（`sd big` 打印）；
  单次调用读 320 块 VERIFY-OK。

### 11.4 根因 C：`sd mkfs` / `sd erase` / `sd big w` 卡死（两个独立 bug）

**现象**：`sd mkfs fat32` 打印 `formatting...` 后**整个 MCU 静默**，
后续命令全无响应，只能复位。`sd erase 63 8000`、`sd big w ... 320` 同样。

**定位手段**：新增 `sd dbg on`（数据通路跟踪，无 libc 依赖，直接写 UART）：

```
>>> sd big w 2000000 320
XFER-try:01408480 00000000        <- n=320(0x140), lba 低16位 0x8480, isRead=0
XFER-st:0000000D 00000030         <- st=13(dataError), ADMA_ERR_STAT=0x30
REC-enter:00000008 00000000       <- 进入恢复...然后没了
```

**Bug 1（致命，Trap）**：`sdmmc_recover_datapath()` 调 CMD12 时传 `NULL_PTR`：

```c
(void)IfxSdmmc_sendCommand(p, IfxSdmmc_Command_stopTransmission, rca << 16,
                           IfxSdmmc_ResponseType_r1b, NULL_PTR);   /* ← 错 */
```

`IfxSdmmc_readResponse()` 对**所有非 CMD0/CMD1 的命令**都会无条件执行
`response->resp01 = 0;` → **向地址 0 写** → 数据访问 Trap，MCU 静默。

* 只有 CMD12 **真的完成**（`commandComplete` 置位）时才会走到 `readResponse()`；
  卡还在忙数据线时 CMD12 在 `sendCommand()` 内部就超时返回，不碰响应。
  这解释了为什么读探测（失败读之后的 CMD12 往往超时）和 shell 的 `sd recover`
  （传的是真实 `&rsp`）一直正常，只有写失败后的恢复路径会炸。
* **修复**：改用局部 `IfxSdmmc_Response rsp;` 传 `&rsp`。

**Bug 2（写失败本身）**：每次模块（重）初始化后，**第一个数据传输必须是读**；
把写作为第一个数据传参会稳定失败（`IfxSdmmc_Status_dataError`），而且重试必然
再失败——因为每次重试都会 `sdmmc_recover_datapath()` → `IfxSdmmc_Sd_initModule()`
重新初始化主机，重新制造同一条件。

* 这也解释了历史现象：「复位后第一次数据传输会失败一次，之后正常」
  「读会 prime 写」——因为 `sd clk meas` 里的预热读先把数据通路点亮了。
* 修复前 `sd mkfs` 的第一笔写（VBR，1 块）就失败 → 恢复 → Trap；
  所以 `sd mkfs` 必死。
* **修复**：`sdmmc_init_module_once()` 初始化成功后补一次**丢弃式 1 块读**
  （`sdmmc_prime_datapath()`，读 LBA 0，最多重试 2 次），
  使 `disk_initialize` 与恢复路径都把数据通路「点亮」。

**附带发现：iLLD 内部轮询超时太短（≈15 ms）**

* `IfxSdmmc_Sd_multiBlockAdma2Transfer()` 用**固定** `IFXSDMMC_TIMEOUT_1E5`
  轮询 `transferComplete`；实测对应约 15 ms 数据阶段。
* 因此 **320 块写（>16 ms）超时**：`st=13 (dataError)`、
  `ADMA_ERR_STAT=0x30`（`ADMA_ERR_STATES=0`、`ADMA_LEN_ERR=0`，即**无 ADMA 错误**）、
  `EISTR=0` → 纯粹是超时，不是描述符/DMA 问题。新复位后 256 块（13.9 ms）仍 PASS。
* 卡越用越慢（内部 GC）会让余量消失，所以**默认单条写限 128 块（64 KB，实测
  8.0 ms）、单条读限 256 块（128 KB）**，留约 2 倍余量；更大的请求自动分块。
  `sd lim <n>` 可覆盖（`sd lim 0` 恢复默认）。

### 11.5 时钟：已经是 50MHz（不是 25MHz）

* `sd info` 显示 `SDCLK=50000 kHz (CLKCTL=0x000F FREQ_SEL=0 PRESET_VAL_ENABLE=1)`：
  `PRESET_VAL_ENABLE=1` 时控制器用**硬件预置分频器**，`PRESET_HS` 的
  `FREQ_SEL_VAL=0` → `100MHz / (2*(0+1)) = 50MHz`（高速模式的预置上限）。
* `sd clk meas` 用数据阶段间接测线速率：512B 块 = 1030 个 SDCLK（4-bit SDR），
  实测 **47269 kHz**（块间间隔使其偏低），与 50MHz 解码一致 → **确认 50MHz 生效**。
* 因此本轮无需再「升到 50MHz」；软件分频器（`sd clk 50000`）与硬件预置等价。
* 注意：**不要在识别完卡之后清 `PRESET_VAL_ENABLE`**（会破坏写通路，见 §10 注释）。

### 11.6 本轮新增/修改的诊断命令

| 命令 | 用途 |
| --- | --- |
| `sd csd` | 分三阶段发 CMD9 并 dump 原始 R2 响应（证明本 IP 不锁存 R2） |
| `sd cap [probe]` | 容量 + 来源 + 探测次数/耗时；`probe` 强制重新探测 |
| `sd dbg <on\|off>` | 打开数据通路跟踪（`XFER-try`/`XFER-st`/`REC-*`/`PRIME`） |
| `sd lim [blocks]` | 单条命令块数上限（默认读 256 / 写 128） |
| `sd big <r\|w> <lba> [blk] [seed]` | 单次调用大块传输 + 描述符/链接/分块统计 |

### 11.7 本轮踩到的坑（新会话必读）

1. **`sd mkfs` / `sd erase` / `sd big w` 卡死不是卡的问题，是 CMD12 响应指针为 NULL 的 Trap**
   （§11.4 Bug 1）。看到「打印一行后整机静默」优先怀疑 Trap。
2. **`build.ps1 -Action download` 不会编译**：`Invoke-Download` 只在 hex 不存在时才 build。
   改了代码必须先 `-Action build` 再 `-Action download`，否则烧的是旧固件（本轮踩过）。
3. **`sd peek/mpeek` 读 SDMMC 模块范围外（≥0x5C）会 Trap 挂死**，需复位。
   已知可用范围 0x00..0x5B。
4. **串口脚本空闲判定**：`sd mkfs` 中途无输出，必须用 `@<秒>:` 前缀给该条命令
   单独放大空闲等待（`sd_shell.py` 支持），否则会被误判为「命令结束」而丢日志。
5. **`sd lim` 默认值不是表容量 505**：读 256 / 写 128 是实测出来的安全值，
   不要想当然按 505 用。
6. **不要用 `sd host v4 0/1` 做实验**：运行中切换 `HOST_VER4_ENABLE` 并重识别
   会把卡/控制器留在 `reinitCard=6` 的坏状态（本轮踩过，需复位）。
7. **`sd mkfs` 很慢是正常的**：f_mkfs 用工作缓冲区逐段填 FAT，`s_mkfsWork`
   从 512B 放大到 8 KB 后 30 GiB FAT32 约 3.7 s。
8. **`sd info` 里的容量来源要看清**：`read probe` 表示 CSD 不可用、容量是探测值。

### 11.8 遗留 / 可选改进

1. **单条命令的块数上限受 iLLD 固定超时限制**。若要真正单条写 505 块，
   需要给 `IfxSdmmc_Sd_multiBlockAdma2Transfer()` 换一个更长的超时
   （本工程选择不改 iLLD，而在胶水层分块）。
2. **容量探测耗时约 1.4 s**（32 次 CMD17，含失败探测的恢复），已缓存，
   只在每次 `disk_initialize` 后第一次取容量时付出。可用 FS BPB 作为
   初值缩小二分区间来加速。
3. **SDSC（字节寻址）卡的容量探测**未实现（本工程只有 SDHC/SDXC 卡）。
4. **`sd poke/peek/mpeek` 保留**，但读未实现地址会 Trap（见 §11.7 坑 3）。

---

## 12 家族问题横向复查（2026-09-22）

§1.3 的"GCC 孤儿段"根因在本工程已经修复。本轮把该修复横向推广到其余 tc397 工程时，
回头复查了本工程，结论是**无需再改动**：

| 检查项 | 结论 |
| --- | --- |
| GCC `-fdata-sections` | 已去掉（保留 `-ffunction-sections`），本轮复核确认 |
| `disk_write_sdmmc()` 的 cache 写回 | 已有 `sdmmc_cache_clean(buff, count*512)`（§11 第 6 条），缓冲放 LMU(cache) 也不会丢数据 |
| lwIP 定时器 / SPI 网口 | 本工程不含 Ethernet 库，不适用 |

**验证**（COM168, 921600，TASKING Debug）：`sd init` / `sd info` 正常——CD P10.7=0 卡在位，
RCA=0x0001，SDCLK=50 MHz，ADMA2 4-bit HS，容量 61067264 sectors（约 29.1 GiB），
FAT32 cluster=64 sectors、free≈29806 MiB；GCC 与 TASKING 均 0 error。

---

## 13 2026-09-22（续）AURIX Development Studio 里的 GCC/TASKING 构建修复

> 用户报告：在 ADS GUI 里编译下载后 **串口有打印但敲回车没反应**（GCC），TASKING 则根本编不过。
> 本轮把 ADS 的托管构建（`.cproject`）与 CMake 构建逐项对齐，定位到两处**只影响 ADS**的缺陷。

### 13.1 根因一：`.cproject` 里没有任何 `-D`，letter-shell 的用户配置被忽略

* CMake 构建给所有源文件传 `-DSHELL_CFG_USER="shell_cfg_user.h"`，而 ADS 的 `.cproject`
  **GCC 配置连 “Defined symbols (-D)” 选项都没有**（TASKING 配置也只有 `__CPU__=tc39xb`）。
* 于是 `shell.c` 编译时 `shell_cfg_user.h` 根本没被包含，`SHELL_TASK_WHILE` 回落到
  `shell_cfg.h` 的默认值 **1**（用户配置要求 0），`Shell`/`ShellCommand` 结构体布局也与
  `shell_port.c`（它直接 include 该头）不一致：`Shell_Process()` 里的 `shellTask()` 变成
  死循环、字段偏移错位 → **上电有打印、输入无响应**。
* **修复**：`Shell/letter-shell/src/shell_cfg.h` 给 `SHELL_CFG_USER` 加默认值
  `"shell_cfg_user.h"`。这样任何构建系统（ADS GUI / CMake / 其它）都不会漏掉用户配置，
  也避开了在 4 个配置 × 9 个工程里各写一遍带引号 `-D`（Eclipse 命令行生成器对引号的
  处理不可控：实测把引号丢掉后 `#include SHELL_CFG_USER` 会直接报错）。

  **实测复现与验证**（GCC Debug，去掉 `-DSHELL_CFG_USER` 等价 ADS）：

  | | 修复前 | 修复后 |
  | --- | --- | --- |
  | 冷启动日志 | 完整打印 | 完整打印 |
  | 敲 `ver` / `mcu` / `help` | **无任何响应** | 全部正常响应 |

### 13.2 根因二：TASKING 缺 `--language=+gcc`，`##__VA_ARGS__` 直接编译失败

* letter-shell 的 `SHELL_EXPORT_CMD()` 用了 GNU 扩展 `, ##__VA_ARGS__`（`shell.h:149/187/260`）。
* ADS 的 TASKING 默认参数只有 `--language=+volatile`（见 ADS 生成的 `subdir.mk`），
  缺 `+gcc` 时 cctc 报 **`ctc E250: missing argument for "..." parameter`**，
  `shell.c` / `shell_port.c` 编译失败 → **ADS 里 TASKING 根本编不过**。
* **修复**：`.cproject` 的两个 TASKING 配置各加一个选项（插件里本就有，只是默认关闭）：

  ```xml
  <option id="com.infineon.aurix.buildsystem.managed.c.compiler.tasking.gcc.<唯一数字>"
          name="Allow GNU C extensions (--language=+gcc)"
          superClass="com.infineon.aurix.buildsystem.managed.c.compiler.tasking.gcc"
          value="true" valueType="boolean"/>
  ```

  **实测**（用 ADS 同款默认参数调 cctc）：

  | 参数 | 结果 |
  | --- | --- |
  | `--language=+volatile`（ADS 默认） | `ctc E250` 多条 → 失败 |
  | `--language=+volatile,+gcc` | 通过 |

### 13.3 一并说明

* ADS GUI 第一次打开工程时会跑 “Project Booster” 同步库，它会自动给工程打补丁
  （本工程被加了 `Shell/shell_port.c` 的 `#include "shell_cfg_user.h"`、`shell_ext.h` 的
  `#include <stddef.h>`），并在 `tc397/.gitignore` 里加上 ADS 的构建目录 —— 都是 ADS 的正常行为。
* ADS 的 GCC 默认参数含 `-fdata-sections`（插件里 `defaultValue="true"`）。已实测：
  ADS 自带的 tricore-gcc11 会生成 `.bss.<sym>`（被 LSL 的 `*(.bss.*)` 收走、启动清零），
  与 Ubuntu gcc13 生成裸 `.<sym>` 的情况不同，**在 ADS 下无副作用**（与 §1.3 的 Ubuntu 情形不同）。
* 验证：GCC 与 TASKING 全量重编 **0 error**；板端 `sd init`/`sd info` 与 shell 命令正常。

---

## 14 Ubuntu26 + GCC13 回归验证（2026-09-22）

* 工具链：`/opt/tricore-gcc` 13.4.1，TAS + `aurix_flasher`，串口 `/dev/ttyACM0` 921600。
* 编译：Debug（`build/gcc`）与 Release（`build/gcc-rel`）均 **0 error**。
* 上板：`sd init`/`sd info` 正常（RCA=0x0001，SDCLK 50MHz ADMA2 4-bit HS，
  29.1GiB FAT32，free≈29806MiB；此前 Windows 下 wedged 的卡本次已恢复）；
  `sd bench`（4MB）**写 4525KB/s、读 11441KB/s**（基线写 4.4~6.1MB/s、读最高 13.8MB/s，
  符合；同卡在 selftest 下另测得写 11906/读 15515KB/s，属卡内 GC 方差）。
* 本次在 Ubuntu 侧无源码改动，Windows（`build.ps1`/TASKING/ADS-GCC）不受影响。

---

## 15 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* FatFs R0.16：ChaN 许可（`Libraries/FatFS/LICENSE` 见 ref/fatfs/LICENSE.txt，
  源码保留原版权头；`mmc_sdmmc.c/diskio.c` 另含 Infineon BSL 头 + 改编注记）
* Letter-Shell：MIT
* 其余移植代码内部许可
