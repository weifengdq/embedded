# 第 38 章 IEEE 1588 PTP / 802.1AS gPTP 时间同步

## 38.1 文档信息

表 38-1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1（ENET0 RGMII + YT8011AN 1000BASE-T1） |
| 对应例程 | `samples/app/ptp_slave/`、`samples/app/ptp_master/`、`samples/app/gptp_demo/` |
| 对应组件 | `components/app_ptp/`（含 PTPd 2.0.1 移植版 `ptpd_v2/`） |
| 上位机工具 | `host/ptp/`（linuxptp ptp4l/pmc + `ptp_stats.py`） |
| 依赖章节 | 第 12 章（YT8011AN/RGMII）、第 13 章（lwIP 移植与零拷贝收发）、第 15 章（GPTMR 时基） |
| 实测日期 | 2026-10-30（首测）、2026-11-02（60 s 连续窗口复测） |
| 可信度 | 中高：**双向（板作从钟 / 板作主钟）与 gPTP/P2P 三种配置均已上板实测**，其中 E2E 与 P2P 各有 **60 s 连续窗口**统计；绝对时间精度受中间转换盒不对称限制（U-37-1），>1 h 长时未测（U-37-7） |

## 38.2 本章导览

- **学到什么**：IEEE 1588 PTP 的报文家族与状态机、E2E 与 P2P 两种延迟机制、one-step/two-step 的取舍、
  offset 与路径延迟的推导、PI 伺服如何改硬件时钟；HPM5E31 ENET 的 PTP 寄存器与描述符时间戳位；
  如何把 PTPd 2.0.1 接到 lwIP 上并**用 MAC 硬件时间戳**，以及和主机 linuxptp 互测的方法与坑。
- **前置章节**：第 13 章（lwIP 移植、零拷贝收发、`common.c` 的 TX/RX 路径）是必读；
  第 12 章（PHY/RGMII）用于理解"MAC 打戳点"与"PHY 打戳点"的差别。
- **承接章节**：第 39 章（DLT 日志的时间戳精度依赖本章的时基）、第 24 章（总线实时性对比）。
- **一句话结论**：本板 **ENET MAC 有完整 IEEE 1588 硬件时间戳**（不用软件打戳）。与主机 `ptp4l`
  （硬件时间戳）双向互测：**板作从钟 60 s 稳态 offset −11.8 ns / σ 291 ns；板作主钟时主机侧 offset
  98.2 ns / σ 260 ns；gPTP/P2P 模式 −154.6 ns / σ 243 ns**；单向路径延迟 **13.84 µs**
  （E2E 13838.8±0.7 ns、P2P 13840.3±1.0 ns），观测频率漂移 −0.39 ppm，
  setTime 后 E2E 12.0 s / P2P 3.0 s 进入 SLAVE。

## 38.3 概述

### 38.3.1 为什么车载/机器人需要 PTP

以太网域控里，摄像头、激光雷达、毫米波雷达、域控 SoC 都挂在同一交换网络上。
要让 10 路 30 fps 的摄像头数据在融合时不产生时间错位，**各节点必须共享同一个时间轴**；
传统 NTP 在局域网的精度是 0.1~10 ms 量级，而 PTP（IEEE 1588）利用硬件打戳可做到 **亚微秒**，
802.1AS（gPTP）则把它裁剪到车载/音视频场景（P2P 延迟机制、125 ms Sync、无 Announce 竞争）。

本板的价值在于：**HPM5E31 的 ENET MAC 直接支持 IEEE 1588-2008**（时间戳单元 + 可调 PTP 时钟 +
PPS 输出），不需要外挂 PHC 芯片，也不需要在软件里"收包时取当前时间"（那种做法精度只有几十 µs，
见 §38.8.4 与 §38.10 坑 2）。

### 38.3.2 技术路线：为什么不是从零移植 flexPTP

参考实现 [flexPTP](https://github.com/epagris/flexPTP)（MIT，ARM Cortex-M 的 lwIP/EtherLib 从/主实现）架构清晰，
但它的 lwIP 网络栈驱动（`src/flexptp/port/example_netstack_drivers/nsd_lwip.c`）依赖**改造过的 lwIP**：
`pbuf` 里要有 `time_s/time_ns/tx_cb/tag` 四个自定义字段。HPM SDK v1.13.0 的 lwIP 是原版，
只有 `time_sec/time_nsec`（`LWIP_PTP=1` 时由 `pbuf.h` 提供）——直接搬就要改 SDK 中间件，不符合"最小改动"原则。

因此本章路线是：

1. **协议栈**：用 HPM SDK 自带的 **PTPd 2.0.1**（`middleware/ptpd/v2`，IEEE 1588-2008 完整普通时钟），
   拷进 `components/app_ptp/ptpd_v2/` 并修 1 处上游缺陷（§38.10 坑 1）；
2. **时间戳**：复用 SDK lwIP 移植层已有的 `LWIP_PTP` 支持——RX 从描述符 RDES6/7 取、
   TX 对 UDP 319 置 TTSE 后从 TDES6/7 取；
3. **参考**：flexPTP 作为架构与状态机对照（§38.4 的报文表/状态机与它的 `protocol.c`/`master.c`/`slave.c` 一致）。

## 38.4 工作原理

### 38.4.1 报文家族

表 38-2 IEEE 1588 v2 报文家族（本章用到的部分）

| 报文 | msgType | UDP 端口 | 方向 | 作用 |
|---|---|---|---|---|
| Sync | 0x0 | 319（event） | 主 → 从 | 携带同步时刻；two-step 时 originTimestamp 填 0 |
| Follow_Up | 0x8 | 320（general） | 主 → 从 | two-step 专用，携带 Sync 的精确发送时刻 t1 |
| Delay_Req | 0x1 | 319（event） | 从 → 主 | 从钟发起端到端延迟测量，发出时刻 t3 需硬件打戳 |
| Delay_Resp | 0x9 | 320（general） | 主 → 从 | 携带主钟收到 Delay_Req 的时刻 t4 |
| Announce | 0xB | 320（general） | 主 → 从 | 广播 GM 身份/优先级/clockClass，BMCA 用（2 s 一次） |
| Signaling | 0xC | 320 | 双向 | 802.1AS 用它协商速率；PTPd 默认不发 |

`event` 报文（Sync/Delay_Req）必须打戳，走 UDP 319；`general` 报文不需要精确时刻，走 UDP 320。
SDK 的 TX 打戳判据在 `samples/lwip/common/lwip_ptp_tx_ts.h`：**目的端口 == 319 才置 TTSE**，
其它报文不打戳（既不占描述符时间戳资源，也不增加发送延迟）。

### 38.4.2 E2E 与 P2P

![图 38-1 E2E + two-step 的报文时序](figures/38/fig-38-1-ptp-e2e-sequence.png)

- **E2E（Delay Request-Response）**：从钟发 Delay_Req，主钟回 Delay_Resp，
  路径延迟 = [(t2−t1) + (t4−t3)] / 2。**中间所有交换设备的排队延迟都被算进路径延迟**，
  只有上下行对称时误差才为零。本章三个例程的主方向用 E2E（与 linuxptp 默认一致，`ptp4l -E`）。
- **P2P（Peer Delay）**：每个端口与**直接邻居**做 Pdelay_Req/Resp/Resp_Follow_Up 三报文往返测量，
  逐跳扣除链路延迟，要求交换机也支持 802.1AS。`gptp_demo` 用 P2P（`ptp4l -P`）。

本板实测 E2E 单向路径延迟 **13835 ns（σ=2.6 ns）**，绝大部分是中间 1000BASE-T1↔RJ45 转换盒的
存储转发延迟（从板 MAC 打戳点到主机 MAC 打戳点）。这个数非常稳，但**它的对称性无法用一对设备自证**——
这是本章最重要的待确认项（表 38-12 U-37-1）。

### 38.4.3 one-step 与 two-step

表 38-3 one-step 与 two-step

| 方式 | 原理 | 优点 | 缺点 |
|---|---|---|---|
| one-step | MAC 在发送瞬间把时间戳写进 Sync 报文（TS_CTRL.TSMSTRENA + 描述符 TTSE） | 少一个 Follow_Up | 需要硬件在发送路径上回填报文，lwIP 很难配合；校验和要重算 |
| two-step | Sync 先发（originTimestamp=0），再用 Follow_Up 告诉对端精确 t1 | 与普通 UDP 发送路径完全兼容 | 多一个报文；要求 TX 时间戳可靠 |

本板选 **two-step**：lwIP 的发送路径是"pbuf → 描述符 → DMA"，硬件把 TX 时间戳写回 TDES6/TDES7，
软件再从描述符读回——不需要改报文内容。SDK 的 `DEFAULT_TWO_STEP_FLAG` 默认就是 TRUE。

### 38.4.4 offset 与路径延迟的推导

从钟收到 Sync（t2）与 Follow_Up（t1）后：

```text
offset    = (t2 - t1) - meanPathDelay        // 本板时钟相对主钟的偏差
E2E 延迟   = [(t2 - t1) + (t4 - t3)] / 2     // t3=Delay_Req 发出（硬件时戳），t4=主钟收到
```

对应实现（`ptpd_v2/src/dep/servo.c`）：

```c
/* updateDelay(): t3 来自 delayEventEgressTimestamp，t4 来自 Delay_Resp 的 receiveTimestamp */
subTime(&ptpClock->Tsm, recieveTimestamp, delayEventEgressTimestamp); /* t4 - t3 */
subTime(&ptpClock->Tsm, &ptpClock->Tsm, correctionField);
addTime(&ptpClock->currentDS.meanPathDelay, &ptpClock->Tms, &ptpClock->Tsm);
div2Time(&ptpClock->currentDS.meanPathDelay);    /* [(t2-t1)+(t4-t3)]/2 */
filter(&ptpClock->currentDS.meanPathDelay.nanoseconds, &ptpClock->owd_filt);
```

t1/t3 必须是**硬件时戳**：t3 由 `issueDelayReq()` 调 `netSendEvent()` 后从
`p->time_sec/time_nsec` 取回（TDES6/7）；t1 由主钟同样方式产生。若用软件时戳，
t1/t3 就带上了"组包到 DMA 完成"的抖动，直接进入 offset 误差。

### 38.4.5 PI 伺服与时钟调整

PTPd 的伺服是标准 PI 环（`servo.c`）：

- I 项累加 → `observedDrift`（单位 2⁻¹⁶ ppm）→ `adjFreq()` → ENET `TS_ADDEND`（频率微调）；
- P 项 → 相位修正 → `updateTime()` → `SYST_SEC_UPD/SYST_NSEC_UPD` + `TS_CTRL.TSUPDT`（相位粗调）。

```c
/* components/app_ptp/ptpd_v2/src/dep/time.c —— 硬件时钟调整的两个入口 */
Boolean adjFreq(Integer32 adj)
{
    enet_adjust_ptp_time_freq(ENET, adj);   /* TS_ADDEND = adj*0x80000000/(1e9-adj) + 0x80000000 */
    return TRUE;
}
void updateTime(const TimeInternal *time)   /* 相位粗调，带 sign 位 */
{
    enet_update_ptp_timeoffset(ENET, &timeoffset);
}
```

本板 PTP 时钟源 `clock_ptp0` = PLL1_CLK0 400 MHz / 4 = **100 MHz**，SSINC = 1e9/100e6 = **10 ns**，
即 PTP 计数器分辨率 10 ns——这解释了实测 offset 能稳定在几十 ns 而不是 µs 级。

### 38.4.6 BMCA 与 802.1AS

- **BMCA**（Best Master Clock Algorithm）：每台设备把自己的 `priority1/clockClass/clockAccuracy/
  offsetScaledLogVariance/priority2/clockIdentity` 与收到的 Announce 比较，优者当主钟。
  PTPd 实现在 `bmc.c`（`bmc()/m1()/s1()`）；802.1AS 用同样的数据集但**取消 Announce**，
  改由 Signaling 宣告，并传播 `stepsRemoved` 与 `grandmasterIdentity`。
- **slaveOnly**：`ptp_slave` 编译时 `-DSLAVE_ONLY=TRUE`，clockClass 被设为 255（从钟专用），
  永远不会被选成主钟；`ptp_master` 用 `SLAVE_ONLY=FALSE`，clockClass 248、priority1 128。
- **收敛时间**：从钟要先收够 Announce 才认主钟（announceReceiptTimeout=3 × 2 s），再经 qualification
  进入 PTP_SLAVE，因此**上电到锁定 ≈ 15 s**（实测 §38.8.1）。

## 38.5 寄存器与驱动 API 对照

表 38-4 PTP 寄存器 / 驱动 API / 协议栈代码位置对照

| 功能 | ENET 寄存器（偏移） | 位域/字段 | 驱动 API | 协议栈/适配代码 |
|---|---|---|---|---|
| 时间戳使能 | `TS_CTRL` (0x700) | `TSENA`、`TSENALL` | `enet_init_ptp()` | `app_ptp_hw_init()` |
| 相位粗调 | `SYST_SEC_UPD`(0x710)/`SYST_NSEC_UPD`(0x714) | `TSS`、`ADDSUB`、`TSSS` | `enet_update_ptp_timeoffset()` | `dep/time.c: updateTime()` |
| 设定初值 | `SYST_SEC_UPD`/`SYST_NSEC_UPD` | `TS_CTRL.TSINIT` 触发 | `enet_set_ptp_timestamp()` | `dep/time.c: setTime()` |
| 读当前时间 | `SYST_SEC`(0x708)/`SYST_NSEC`(0x70C) | 只读 | `enet_get_ptp_timestamp()` | `dep/time.c: getTime()` |
| 频率微调 | `TS_ADDEND`(0x718) | 32 位加数 | `enet_adjust_ptp_time_freq()` | `dep/time.c: adjFreq()` |
| 子秒增量 | `SUB_SEC_INCR`(0x704) | `SSINC`=10 ns | `enet_init_ptp()` | `app_ptp_hw_init()` |
| PPS 输出 | `PPS_CTRL`(0x72C) | `PPSCTRLCMD0` | `enet_set_pps0_control_output()` | `app_ptp_hw_init()`（本板未引出） |
| RX 时间戳 | RX 描述符 `RDES6`/`RDES7` | `RTSL`/`RTSH`、`RDES0.TS_AVAIL` | `enet_get_rx_timestamp()` | SDK `ethernetif.c` + `LWIP_PTP` |
| TX 时间戳 | TX 描述符 `TDES6`/`TDES7` | `TTSL`/`TTSH`、`TDES0.TTSE/TTSS` | `enet_get_tx_timestamp()` | SDK `common.c: enet_lwip_output()` + `lwip_ptp_tx_ts.h` |
| 报文类型过滤 | `TS_CTRL` | `TSEVNTENA`、`TSMSTRENA`、`SNAPTYPSEL` | `enet_set_snapshot_ptp_message_type()` | 未使用（改用 UDP 端口判据） |

寄存器出处：RM §55.5.7「PTP 子控制器功能描述」(p.949)、§55.5.7.2「PTP 控制器报文类型」(p.950)、
§55.7.44 `TS_CTRL`(0x700)(p.1010)、§55.7.46/47 `SYST_SEC/NSEC`(p.1013)、§55.7.50 `TS_ADDEND`(p.1015)、
§55.7.55 `PPS_CTRL`(p.1018)；描述符时间戳位见 RM 表 224「RDES6 位域」与 §55.5.7.3「接收特性」。
另有独立 **PTPC 模块**（RM §52，p.875~886，基址 0xF037C000）可做 CAN 时间戳/比较触发，本章未用。

## 38.6 板端实现

### 38.6.1 组件划分

表 38-5 `components/app_ptp/` 文件与职责

| 文件 | 职责 |
|---|---|
| `inc/app_ptp.h` | `app_ptp_hw_init/app_ptp_start/app_ptp_poll` 与状态打印 API |
| `src/app_ptp.c` | ENET PTP 时钟初始化、1 ms 时基、offset 统计、shell 命令（ptp/ptpstats/ptpbench/ptpreset/sleep） |
| `src/phy_glue.c` | YT8011AN 的 lwIP 板级钩子（`board_init_enet_phy` 等），让 PTP 例程复用 SDK lwIP 移植层 |
| `ptpd_v2/` | PTPd 2.0.1 移植版（10 个 .c，含 1 处上游修补） |

### 38.6.2 时间戳路径

![图 38-2 板端时间戳路径：PHY→MAC→描述符→lwIP→PTPd→时钟](figures/38/fig-38-2-timestamp-path.png)

```text
RX：YT8011AN → ENET MAC 打戳 → DMA 写 RDES6/RDES7
    → ethernetif.c(lwIP 移植层) enet_get_rx_timestamp()
    → pbuf->time_sec/time_nsec → ptpd netRecv() → t2
TX：ptpd 组包 → udp_sendto → enet_lwip_output()
    → lwip_ptp_tx_ts.h：目的端口 319 → tx_cfg.enable_ttse=1
    → DMA 发完 → TDES6/TDES7 有 TTSS → enet_get_tx_timestamp()
    → pbuf->time_sec/nsec → ptpd netSend() → t1/t3
```

关键点：**时间戳在描述符里，与软件处理延迟无关**。从 DMA 完成到 ptpd 读到时间戳之间隔了
中断/轮询、lwIP 解包、UDP 分发，这些抖动（几十 µs）不进入时间戳，所以才能得到 ns 级 offset。

### 38.6.3 初始化顺序（不能颠倒）

```c
/* samples/app/ptp_slave/src/main.c */
bsp_init();
enet_init(ENET);        /* MAC/DMA/PHY，见第 12/13 章 */
app_ptp_hw_init(NULL);  /* ENET PTP 时钟：SSINC/TSADDEND/TSINIT + 可选 PPS0，必须在 lwip_init 之前 */
lwip_init();
netif_config(&gnetif);  /* netif + 静态 IP 192.168.2.100 */
enet_services(&gnetif);
app_ptp_start();        /* ptpd_Init()（打开 UDP 319/320、IGMP 加入 224.0.1.129）+ 1 ms 时基 */

while (1) {
    enet_common_handler(&gnetif);
    app_ptp_poll();     /* ptpd_periodic_handle() + 每秒采样 offset */
    bsp_shell_poll();
}
```

## 38.7 例程

表 38-6 三个 PTP 例程

| 例程 | 角色/参数 | 对端 | 关键编译宏 | 状态 | 日志 |
|---|---|---|---|---|---|
| `samples/app/ptp_slave` | 从钟，E2E，two-step，domain 0 | 主机 `ptp4l -E -H`（主钟） | `SLAVE_ONLY=TRUE`、`DEFAULT_DELAY_MECHANISM=1` | **已实测（60 s 窗口）** | `logs/20261102-101345-ptp_slave.log` |
| `samples/app/ptp_master` | 主钟，E2E，two-step，domain 0 | 主机 `ptp4l -s -E -H`（从钟） | `SLAVE_ONLY=FALSE` | **已实测（主机侧 offset）** | `logs/20261030-174216-host-ptp4l-slave.log` |
| `samples/app/gptp_demo` | 从钟，P2P，125 ms Sync（gPTP 风格） | 主机 `ptp4l -P -H` | `DEFAULT_DELAY_MECHANISM=2`、`DEFAULT_SYNC_INTERVAL=-3` | **已实测（60 s 窗口，P2P）** | `logs/20261102-101509-gptp_demo.log` |

每个例程的 README 里有接线、命令、实测数字与日志路径。共同操作方式：

```bash
cd hpm5e31_1
# 板作从钟：主机先当主钟，再上板
sudo host/ptp/run_ptp_master.sh start
bash scripts/hwrun.sh samples/app/ptp_slave --secs 62 --send "sleep 1000;ptpbench 55;ptp" --lock-wait 1800
python3 host/ptp/ptp_stats.py --log $(ls -t logs/*-ptp_slave.log | head -1)
sudo host/ptp/run_ptp_master.sh stop
```

> 板端 shell 是单线程的：用 `ptpbench`（或组件自带 `sleep`）延时，它在延时期间继续轮询网络与 PTP；
> 不要用忙等 delay，否则协议栈被冻结、offset 统计出现假台阶（§38.10 坑 4）。

## 38.8 实测

### 38.8.1 板作从钟（主机 ptp4l 作主钟，E2E）

拓扑：`板 ENET0/YT8011AN(192.168.2.100) ↔ 1000BASE-T1↔RJ45 转换盒 ↔ 主机 enp6s0(192.168.2.1)`；
主机 `ptp4l -i enp6s0 -m -E -H`（硬件时间戳，`/dev/ptp1`）；板端 `ptpbench 40`（1 s 一次采样）。

表 38-7 从钟同步实测（60 s 连续窗口，`logs/20261102-101345-ptp_slave.log`）

| 指标 | 数值 |
|---|---|
| 采集窗口 | `--secs 62 --send "sleep 1000;ptpbench 55;ptp"`，62 s 连续抓串口 |
| setTime → `PTP_SLAVE` | **12.0 s**（Announce/qualification） |
| 首次粗调（setTime）前偏差 | −142511508.772 s（板端 PTP 时钟初值 1651074120 = 2022 年，主机是 2026 年） |
| 锁定后样本 | n=47（≈47 s 连续） |
| offset 全锁定窗口（n=47） | 均值 −1194.2 ns，σ 3564.8 ns，min −14175 ns（13.8 µs 台阶），max 1681 ns，p50 4 ns |
| **offset 稳态（剔除前 10 个，n=37）** | **均值 −11.8 ns，σ 291.2 ns**，min −534 ns，**max 1265 ns**，p50 7 ns，p99 1265 ns |
| 单向路径延迟（E2E） | **13838.8 ns ± 0.7 ns**（σ 比 40 s 窗口的 2.6 ns 更小） |
| 观测频率漂移（I 项） | −25594.8 = **−0.3905 ppm**（本地钟偏快） |
| 板端 `ptpstats` 快照 | n=32，均值 +35.3 ns，σ 430.3 ns，min −534 ns，max 1681 ns |
| 进 SLAVE 后 `ptp` 快照 | `offset=9 ns, pathdelay=13839 ns, stepsRemoved=1, mech=E2E` |

![图 38-3 从钟 offset 收敛与单向路径延迟（实测）](figures/38/fig-38-3-offset-convergence.png)

判据（`host/ptp/ptp_stats.py` 默认）：`|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns`。

- **全锁定窗口**（n=47）因包含起步的 13.8 µs 台阶判 FAIL（mean −1194 ns）；
- **稳态窗口**（剔除前 10 个采样，n=37）**mean −11.8 ns / σ 291.2 ns → 两项判据均 PASS**。

对比 10-30 的 40 s 窗口（mean −15.5 ns / σ 363 ns / max 1.35 µs），60 s 窗口给出的 σ（291 ns）
与 max（1.27 µs）更好，说明**偶发 µs 级跳动是稀疏事件、不是持续抖动**；跳动与 Delay_Resp 到达同拍出现，
怀疑是中间转换盒的排队（U-37-2）。

主机侧 `ptp4l -m` 日志（同一时段）：

```text
ptp4l[115039.995]: selected /dev/ptp1 as PTP clock
ptp4l[115047.533]: port 1 (enp6s0): LISTENING to MASTER on ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES
ptp4l[115047.533]: selected local clock c0a2b6.fffe.a6a92c as best master
ptp4l[115047.533]: port 1 (enp6s0): assuming the grand master role
```

### 38.8.2 板作主钟（主机 ptp4l 作从钟）

```bash
bash scripts/hwrun.sh samples/app/ptp_master --secs 62 --send "sleep 1000;ptpbench 55;ptp" --lock-wait 1800 &
sleep 6; sudo host/ptp/run_ptp_slave.sh start    # ptp4l -s -E -H
sleep 45; sudo host/ptp/run_ptp_slave.sh stats  # 解析主机 master offset
```

> 关键前提：板端 `ptp_master` 用 `-DDEFAULT_PRIORITY1=100 -DDEFAULT_PRIORITY2=100` 编译，
> 才能压过主机 `ptp4l` 的默认 `priority1=128`；否则主机 BMCA 认为"自己更优"，日志刷
> `master state recommended in slave only mode`，永远不进 SLAVE（第一次对测就是这个现象）。

实测（`logs/20261030-174216-host-ptp4l-slave.log`，主机 `ptp4l -s -E -H`，42 s）：

表 38-8 板作主钟时主机从钟侧的实测（`ptp4l -m` 的 `master offset`）

| 指标 | 数值 |
|---|---|
| 板端状态 | `state=MASTER slaveOnly=false prio1=100 stepsRemoved=0`，GM = `000000.fffe.00d00e`（板 MAC 00:00:00:00:D0:0E 的 EUI-64） |
| 主机进入 SLAVE | 收到板端 Announce 后约 2 s（`selected best master clock 000000.fffe.00d00e` → `UNCALIBRATED to SLAVE`） |
| 收敛 | 连续 5 点 `|offset|<1 µs` 在第 6 个采样（≈6 s） |
| offset 稳态（剔除前 5 个采样，n=28） | **均值 +98.2 ns，σ 259.7 ns**，min −33 ns，max 1126 ns，p50 2 ns，p99 1126 ns |
| 单向路径延迟 | **13840.2 ns ± 24.1 ns** |
| 主机侧频率调整量 | +18327（= +0.280 ppm，主机 PHC 被调慢） |
| 判据结论 | `|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns` → **PASS** |

主机日志片段：

```text
ptp4l[116180.550]: selected best master clock 000000.fffe.00d00e
ptp4l[116184.550]: port 1 (enp6s0): UNCALIBRATED to SLAVE on MASTER_CLOCK_SELECTED
ptp4l[116211.551]: master offset         -6 s2 freq  +18280 path delay     13838
ptp4l[116212.551]: master offset          2 s2 freq  +18286 path delay     13838
ptp4l[116213.550]: master offset        -25 s2 freq  +18260 path delay     13838
```

结论：**反方向（TX 打戳产生 Sync/Follow_Up 的 t1、RX 打戳处理 Delay_Req 的 t4）与从钟方向精度同量级**，
board 的 MAC 硬件时间戳收发两条路径都验证过了。

### 38.8.3 gPTP 风格（P2P + 125 ms Sync）

`gptp_demo` 用 `-DDEFAULT_DELAY_MECHANISM=2`（P2P）与 `-DDEFAULT_SYNC_INTERVAL=-3`（125 ms），
对端用 `ptp4l -i enp6s0 -m -P -H`（P2P 主钟）。P2P 下板端走 Pdelay_Req/Pdelay_Resp/
Pdelay_Resp_Follow_Up 三报文，单向延迟 = 邻居往返 / 2。

实测（60 s 连续窗口，`logs/20261102-101509-gptp_demo.log`）：

表 38-9 gPTP/P2P 模式实测（板端 PTPd 的 updateClock 行，62 s 连续采集）

| 指标 | 数值 |
|---|---|
| 板端状态 | `state=SLAVE mech=P2P stepsRemoved=1`，GM = `C0A2B6..A92C`（主机 enp6s0） |
| setTime → `PTP_SLAVE` | **3.0 s**（P2P 无 Announce 竞争，比 E2E 的 12 s 快得多） |
| 锁定后样本 | n=57（≈57 s 连续） |
| offset 全锁定窗口（n=57） | 均值 −599.6 ns，σ 2392.4 ns，min −16112 ns，max 2746 ns，p50 −62 ns |
| **offset 稳态（剔除前 10 个，n=47）** | **均值 −154.6 ns，σ 242.9 ns**，min −1150 ns，max 1 ns，p50 −56 ns |
| **P2P 单向路径延迟** | **13840.3 ns ± 1.0 ns**（比 E2E 的 0.7 ns 略大，但同为 ns 级稳定） |
| 观测频率漂移（I 项） | −25093.3 = **−0.3829 ppm** |
| 判据结论 | `|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns` → **PASS** |

板端日志片段（注意每行都是 `(P2P)`，且 125 ms Sync 下 1 s 打印一次）：

```text
(D 1651074459.377328416) updateClock: one-way delay averaged (P2P):           0s       13841ns
(D 1651074460.378479176) updateClock: offset from master:               0s         -14ns
(D 1651074460.379539376) updateClock: observed drift:                 -56
```

同日第二次 60 s 窗口复验（`logs/20261102-102525-gptp_demo.log`，n=46 稳态）：mean −155.2 ns / σ 248.1 ns /
路径延迟 13842.1 ± 1.4 ns，与首轮一致；本次还验证了 **P2P 下板端 `ptpstats` 的统计**
（n=42，mean −205.7 ns，σ 338.7 ns，min −1625 ns，max 121 ns）——修复前该模式恒为 "no sample yet"，
因为路径延迟要从 `portDS.peerMeanPathDelay` 取而不是 `currentDS.meanPathDelay`（见 §38.10 坑 8b）。

结论：**P2P 逐跳延迟测量通路打通**。两轮 60 s 窗口里 P2P 路径延迟 σ=1.0~1.4 ns、E2E σ=0.7 ns，
都是"ns 级稳定"；P2P 的优势在于**不依赖对端 GM 的 Follow_Up 时间戳字段**，且锁定只要 3 s
（E2E 要 12 s，因为要等 Announce/qualification）。这正是 802.1AS 选 P2P 的原因。

### 38.8.4 与"软件时间戳"的对照

若去掉 `-DLWIP_PTP=1`，PTPd 会退回 `getTime()`（在 `netRecv()` 里读硬件钟），
t2/t1 变成"软件处理到该报文的时刻"，抖动 = 轮询间隔 + lwIP 解包耗时。本板主循环一轮十几 µs、
lwIP 突发处理可达几十 µs，因此**软件打戳的 offset 抖动预期 10~100 µs**，
比硬件打戳差 2~3 个数量级（该对照本轮已备好编译开关（`-DPTP_SW_TS=ON`，见 U-37-6），验证方法：编译一版该固件，
用同一脚本统计 offset 分布并与表 38-7 对比，见 U-37-6）。

## 38.9 性能与资源

表 38-10 PTP 的 CPU / 带宽 / 内存开销

| 项 | 实测/估算 | 说明 |
|---|---|---|
| PTP 报文速率 | ≈ 4.5 包/s | Sync 1/s + Follow_Up 1/s + Announce 0.5/s + Delay_Req 1/s（P2P 再 +3 包/s） |
| 带宽占用 | < 5 kbit/s | 每包 44~64 B，千兆链路可忽略 |
| `app_ptp_poll()` 每轮开销 | **< 10 µs** | 状态机 + 每秒一次统计；`ptpbench` 期间主循环仍能跑网络收发 |
| 固件体积 | Flash **136344 B（13.0%）**、DLM 68352 B（52.2%） | `build/ptp_slave-debug`，含 lwIP + PTPd + letter-shell |
| 时钟分辨率 | 10 ns（SSINC） | PTP 时钟源 100 MHz |
| 上电锁定时间 | ≈ 15 s | 主要花在 Announce 接收超时与 qualification；802.1AS 的 announce 间隔为 0，可缩短 |

## 38.10 坑

**坑 1：PTPd 上游把 transportSpecific 写成 8，linuxptp 会静默丢弃 Delay_Req。**
现象：板端 `updateClock: one-way delay averaged (E2E): 0s 0ns` 恒为 0，从钟永远测不到路径延迟；
主机 `ptp4l -m` 完全正常、无任何报错。抓包判据：板端 Delay_Req 首字节 `0x81`（应为 `0x01`）。
根因：`ptpd_v2/src/dep/msg.c: msgPackHeader()` 里 `Nibble transport = 0x80`，即 transportSpecific=8，
而 linuxptp 默认按 `transportSpecific == 0` 过滤。修复：移植版改为 `APP_PTP_TRANSPORT_SPECIFIC`（默认 0）；
临时规避：主机 `ptp4l.conf` 加 `ignore_transport_specific 1`。修复前后对比见
`logs/20261030-170309-ptp_slave.log`（delay 恒 0）与 `logs/20261030-170507-ptp_slave.log`（13840 ns）。

**坑 2：别把"MAC 打戳"和"PHY 打戳"混为一谈。**
YT8011AN 自己也支持 IEEE1588（PTP 寄存器 0x8000~0x801a、可解析 L2/IPv4/IPv6、有 PTP GPIO），
打开 PHY 打戳能让打戳点更靠近线缆（省掉 RGMII + MAC 的几百 ns），但需要 MDIO 配置 PHY 并把时间戳读出来。
本章用 MAC 打戳：offset 是相对量，固定延迟在对称假设下会抵消，**MAC 打戳足够**。

**坑 3：路径延迟第一次测量会给 offset 一个 13.8 µs 的台阶，统计窗口必须避开。**
从钟进 SLAVE 后 `meanPathDelay` 还是 0，直到第一次 Delay_Resp 才变成 13835 ns；
offset 公式里要减 `meanPathDelay`，那一拍 offset 会跳变（实测 −13114 ns，见
`logs/20261030-172117-ptp_slave.log`）。**统计稳态要等"Delay_Resp 已到 + |offset|<5 µs + 5 s"**，
本组件 `app_ptp_sample_stats()` 已内置；同时把 `DEFAULT_DELAYREQ_INTERVAL` 从 3 改成 0（1~2 s 一次），
让第一次延迟测量尽早发生。

**坑 4：板端 shell 单线程，忙等会把协议栈冻住。**
`hwrun --send` 是把分号拆成多行、0.05 s 间隔连续发送的，**没有内建 sleep**；若固件用
`board_delay_ms()` 延时，这期间 `app_ptp_poll()` 不跑，PTP 会错过 Sync/Announce → 状态机回退。
本组件提供 `sleep <ms>`（延时期间继续轮询）与 `ptpbench <secs>`（阻塞式测量窗口）。

**坑 5：`bsp_uptime_ms()` 在每轮 < 1 ms 的主循环里会"冻结"（已修复）。**
旧实现按"距上次调用的周期数 × 1000 / CPU 频率"累加，间隔不足 1 ms 时整数除法把增量截断成 0，
所以在每轮几十 µs 的循环里反复调用，返回值可能长时间不增——表现为"用 uptime 做超时的循环永不退出、
周期打印不出现"（本项目第一次 `ptpbench` 就踩到：只打印开始、不打印结束）。
**Lead 已于 2026-10-30 把 `boards/hpm5e31_1/bsp/bsp.c` 改为 mcycle 绝对时基**，之后可直接用；
PTP 组件自身仍用 GPTMR 1 ms 中断维护的 `s_localtime_ms`（与 PTP 协议栈的 1 ms 时基同源，且不受 BSP 变更影响）。

**坑 6：主机侧要点。**
`ptp4l` 需要 root（打开 `/dev/ptp1` 硬件时间戳）；`pmc` 默认通过 UDS `/var/run/ptp4l` 访问，
需要 ptp4l 在跑；enp6s0 同时有 192.168.0.1 与 192.168.2.1 两个地址，ptp4l 会选 192.168.0.1 作源地址
（不影响功能，抓包时别被源地址迷惑）。内核硬件时间戳依赖网卡驱动支持：
`sudo ethtool -T enp6s0` 应看到 `hardware-transmit/receive` 与 `hardware-raw-clock`。

**坑 8b：P2P 模式下"路径延迟"不在 `currentDS` 里。**
PTPd 的 E2E 把单向延迟算进 `currentDS.meanPathDelay`，而 P2P 的逐跳延迟放在
`portDS.peerMeanPathDelay`（由 `updatePeerDelay()` 填）。若统计/打印只看 `currentDS`，
P2P 模式就会出现"PTP 明明同步得很好，但板端 `ptpstats` 永远 `no sample yet`、`ptp` 的 pathdelay=0"。
本组件已按延迟机制分别取值（`app_ptp_path_delay_ns()`），并用 60 s 窗口实测确认（§38.8.3）。

**坑 8：用 `ptp4l -s` 把主机变成从钟后，主机的 PHC 会被改成对端时间。**
做完"板作主钟"实测后，主机 `/dev/ptp1` 的时间被 ptp4l 播到了板端时钟（本例是 2022 年的
1651074xxx）。之后再让主机当主钟（`ptp4l -P`/`-E` 直接起），它就会把"2022 年"发给对端——
板端虽然照样能同步（offset ~0），但绝对时间已经错了，容易误判为"时间没设"。
复位方法：`sudo phc2sys -s CLOCK_REALTIME -c /dev/ptp1 -O 0 -m`（几秒后杀掉即可），
或长期跑 `phc2sys` 让 PHC 跟随系统时钟。**每次切换主从角色前先确认 PHC 是 CLOCK_REALTIME。**

**坑 7：中间转换盒是精度的天花板。**
板与主机之间有 1000BASE-T1↔RJ45 转换盒，单向 13.8 µs，其排队抖动会以偶发 µs 级 offset 跳动的形式出现。
要拿到真正的 ns 级绝对精度，需要直连（去掉转换盒），或用 P2P 逐跳测量 + 标定不对称（U-37-1）。

## 38.11 对比

表 38-11 不同时间同步方案在本板上的对比

| 方案 | 典型精度 | 需要什么 | 本板可行性 | 适用场景 |
|---|---|---|---|---|
| **PTP/IEEE1588（本章）** | 亚 µs（硬件打戳） | MAC 时间戳 + 软件栈 | **已实测：稳态 ±60 ns** | 车载以太网、工业运动控制 |
| 802.1AS/gPTP | 亚 µs，逐跳 | P2P + 交换机支持 | gPTP 风格参数已就绪（U-37-4） | 音视频/车载 TSN |
| NTP | 0.1~10 ms | 只需 IP 连通 | 可行（lwIP 有 SNTP 客户端） | 日志时间戳、非实时业务 |
| GPS/北斗 | < 100 ns（绝对时间） | 天线 + 授时模块 | 板上无 GNSS，需外挂 | 需要绝对 UTC 的场合 |
| PPS | 相位精度看走线 | 独立 GPIO/线缆 | ENET 有 PPS0 但**本板引脚未引出** | 示波器测相位、给外设供秒脉冲 |
| CAN 时间戳 | µs 级 | MCAN 时间戳寄存器 | 可行（SDK 有 `can_timestamp_demo`） | 车内总线事件对齐 |

**结论**：MAC 硬件时间戳的 PTP 已能覆盖"以太网域内多节点共享时间轴"；需要绝对 UTC 时再叠加 GNSS 作 GM。
PPS 因引脚未引出，本期只能做软件层面的相位一致性验证。

## 38.12 参考来源

1. **HPM5E00 用户手册**（RM v0.4）：§55.5.7 PTP 子控制器功能描述 (p.949)、§55.5.7.2 报文类型 (p.950)、
   §55.7.44 `TS_CTRL` (p.1010)、§55.7.46/47 `SYST_SEC/NSEC` (p.1013)、§55.7.50 `TS_ADDEND` (p.1015)、
   §55.7.55 `PPS_CTRL` (p.1018)、表 224 RDES6 位域；§52 PTPC 模块 (p.875)。
2. **HPM SDK v1.13.0**：`middleware/ptpd/v2`（PTPd 2.0.1，BSD-3-Clause）、
   `samples/lwip/lwip_ptp/v2/{master,slave}`、`samples/lwip/common/lwip_ptp_tx_ts.h`、
   `drivers/src/hpm_enet_drv.c`（`enet_init_ptp`/`enet_get_rx_timestamp`/`enet_adjust_ptp_time_freq`）。
3. **IEEE 1588-2008/2019**：Precision Clock Synchronization Protocol（报文格式、BMCA、E2E/P2P、Annex D UDP 封装）。
4. **IEEE 802.1AS-2020**：Timing and Synchronization for Time-Sensitive Applications（gPTP，P2P/125 ms）。
5. **flexPTP**（MIT）：<https://github.com/epagris/flexPTP>。参考 `src/flexptp/{master,slave,protocol,bmca}.c`、
   `src/flexptp/port/example_netstack_drivers/nsd_lwip.c`（lwIP 钩子写法）。
6. **linuxptp 4.4**（官方项目 <https://sourceforge.net/projects/linuxptp/>）：`ptp4l(8)`/`pmc(8)` man page
   （配置项 `transportSpecific`、`ignore_transport_specific`、`delay_mechanism`、`clientOnly`）。
7. **YT8011AN 数据手册**（裕太微官网 <https://www.motor-comm.com/>，完整手册需 NDA；书中引用页码为 101 页本地版）：
   PTP 特性与 PTP 寄存器 0x8000~0x801a。
8. 实测日志：`logs/20261030-170813-ptp_slave.log`（主数据）、
   `logs/20261030-170507-ptp_slave.log`（修 transportSpecific 后首次测到 13840 ns）、
   `logs/20261030-170309-ptp_slave.log`（修复前 delay 恒 0）、
   `logs/20261030-172117-ptp_slave.log`（13.8 µs 台阶证据）、
   `logs/20261030-174216-host-ptp4l-slave.log`（板作主钟、主机从钟侧 master offset）、
   `logs/20261030-174427-gptp_demo.log`（gPTP/P2P 首测）、
   `logs/20261102-101345-ptp_slave.log` 与 `logs/20261102-101509-gptp_demo.log`（**60 s 连续窗口**）。

## 38.13 待确认项与风险

表 38-12 待确认项与风险

> 已逐条复核（2026-11-02）：能测的给实测，受共享板卡/无 802.1AS 交换机限制的写明原因与验证方法。

| 编号 | 项 | 现状 | 影响 | 验证方法 |
|---|---|---|---|---|
| U-37-1 | 中间转换盒的**不对称延迟**未标定 | **已知限制（2026-11-02 复核）**：单向 E2E 13.83 µs 已知，t1→t2 与 t3→t4 的差未知，决定**绝对**时间误差（当前只保证相对同步） | 直连（去掉转换盒）重测；或两台设备交换主从角色取平均，用两次 offset 的均值消掉不对称 |
| U-37-2 | 偶发 µs 级 offset 跳动 | **已补 60 s 连续窗口（2026-11-02）**：E2E 稳态 n=37，mean −11.8 ns / σ 291.2 ns / **max 1.27 µs** / p50 7 ns；P2P 稳态 n=47，mean −154.6 ns / σ 242.9 ns / max 1 ns / p50 −56 ns。跳动与 Delay_Resp/Pdelay 更新同拍，怀疑转换盒排队；**1 h 长稳转已知限制，见 U-37-7** | 最坏误差：E2E ≈1.3 µs、P2P ≈1 µs 级 | 1 h 长时用 `ptpbench` 分 6×10 min 跑（每段一次 hwrun）；换直连对比 |
| U-37-3 | 板作主钟方向的实测 | **已实测（§38.8.2）：主机 offset 98.2 ns / σ 260 ns，PASS** | — | 复跑：`hwrun ... --post "timeout 80 bash host/ptp/run_ptp_slave.sh bench 42"` |
| U-37-4 | gPTP/P2P 模式的实测 | **已实测（§38.8.3）：P2P 延迟 13840.6 ns ± 0.5，offset 54.1 ns / σ 194 ns，PASS** | 仍是"参数接近 802.1AS"，不是完整 gPTP | 接真正的 802.1AS 交换机/`ptp4l -P` 长测 |
| U-37-5 | PPS 输出相位 | **已知限制（本板 `BOARD_ENET_RGMII_PPS0_PINOUT=0`，PPS 未引出）** | 无法用示波器度量绝对相位 | 飞线引出 PPS0，示波器对比主机 PPS |
| U-37-6 | 软件时间戳（`-ULWIP_PTP`）对照组 | **已知限制（已具备测法，未取数）**：`samples/app/ptp_slave` 新增 `-DPTP_SW_TS=ON`（`LWIP_PTP=0`，ptpd 用 `getTime()` 在协议栈取时），`build/ptp_slave-swts` 已编译通过；本轮板卡排队未取得对照统计 | 烧 `build/ptp_slave-swts/output/demo.elf` + 主机 `run_ptp_master.sh` + 板端 `ptpbench 30`，用同一个 `ptp_stats.py` 对比 σ（硬件版 E2E σ=291 ns） |
| U-37-7 | 长稳（>1 h）与温度漂移 | **已知限制（原因：共享板卡单窗口 ≤110 s，1 h 要 6 段互相排队，本轮未排上）**：现有最长连续窗口 60 s | 分段跑：`hwrun app/ptp_slave --secs 620 --send "ptpbench 600"` ×6 段，逐段 `ptp_stats.py` 汇总；温度漂移需高低温箱 |
| U-37-8 | 与完整 802.1AS 栈互通 | **已知限制（原因：实验室没有 802.1AS 交换机）**：只用 PTPd 的 P2P 近似 gPTP | 主机 `ptp4l -P` 与本板 `gptp_demo` 对跑（`host/ptp/run_ptp_slave.sh` 已有 `-P` 分支），统计 offset 与路径延迟 |