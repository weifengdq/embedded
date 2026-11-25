# 第 29 章 ISO-TP 传输层：从帧格式到 4095 字节双向收发

## 29.1 文档信息

| 项 | 内容 |
|---|---|
| 章版本 / 日期 | v1.0 / 2026-10-30 |
| 适用芯片 / 板卡 | HPM5E31IPB1（HPM5E00 系列）/ hpm5e31_1 最小系统板 |
| 协议标准 | ISO 15765-2:2016（ISO-TP，又称 CAN-TP） |
| 仓库组件 | `components/app_isotp/`（协议栈 + MCAN 端口） |
| 对应例程 | `samples/app/isotp_echo/` |
| 上位机 | `host/uds/isotp_test.py`（裸实现 + can-isotp 双实现） |
| 实测日期 | 2026-10-30 16:55 ~ 17:05，2026-11-02 12:09 补测（Bench A，板内双节点） |
| 主日志 | `logs/20261030-165808-isotp_echo.log` |
| 辅助日志 | `logs/20261030-165657-isotp_echo.log`（BS/STmin 27 组）、`logs/20261030-165559-isotp_echo.log`（首轮 PASS） |
| 可信度 | 全部数字来自上板串口日志；6000 B 转义、200 次高负载、CF 间隔为 2026-11-02 实测；理论值单独标注"实测/推算" |

## 29.2 本章导览

读完本章你应该能：

1. 看着 CAN 报文就知道它是一条 ISO-TP 消息的哪一段（SF/FF/CF/FC），以及还剩多少帧；
2. 自己写一个能把 4095 字节在 500 kbit/s 经典 CAN 上稳定收发的状态机，并知道 BS/STmin/超时怎么配；
3. 判断一条诊断链路的理论吞吐上限，以及为什么 CAN FD 能把同一份数据快 6.5 倍；
4. 避开 padding、DLC 编码、FC.WAIT、多帧交错、STmin 语义这五个最常见的易错点。

前置章节：第 08 章（CAN FD 节点）、第 09 章（CAN 性能与总线负载）。
后续章节：第 30 章（UDS，跑在 ISO-TP 之上）、第 28 章（CAN 网关，把 ISO-TP 帧搬到主机 vcan0）。

## 29.3 概述与典型应用

ISO-TP 解决一个很具体的问题：**CAN 一帧最多 8 字节（经典）或 64 字节（FD），而诊断消息动辄几十到几千字节**。
它把长消息切段、编号、加流控，让接收方来得及收、发送方不会把总线打爆。
UDS（第 30 章）、KWP2000、OBD-II 的诊断报文全部跑在 ISO-TP 上；J1939 的 TP.CM/TP.DT 是同类思路的另一套实现。

| 特性 | 本实现支持情况 | 说明 |
|---|---|---|
| 单帧 / 首帧 / 连续帧 / 流控帧 | 全部 | 2016 版 N_PCI 编码 |
| 消息长度 | 1 ~ 4095 B（经典）/ 8192 B（FD 扩展） | FF_DL 12 位，FF_DL=0 时用 32 位长度 |
| 流控 | BS（BlockSize）+ STmin，CTS/WAIT/OVFLW | 收发双向都遵守 |
| 时间参数 | N_As / N_Ar / N_Bs / N_Cr | 见 30.4.4 |
| padding | 0x00/0xCC/0xAA 可配，经典补到 8 B，FD 补到合法 DLC | 实测把尾帧补成 0xAA 上总线 |
| 寻址 | 11 位 / 29 位；1 字节地址扩展（AE） | 29 位实测 0x18DAF110/0x18DA10F1 |
| 多通道并发 | 每通道一个实例，CAN0/CAN1 同时跑 | 板内 DUT + tester 就是两个实例 |
| CAN FD | DLC>8、SF 转义形式、FC 帧 64 B、BRS | 见 30.4.5 |

典型应用：ECU 诊断（0x7E0/0x7E8）、刷写（0x34/0x36/0x37）、OBD-II 排放扫描、产线 EOL 检测。
在 HPM5E31 这类"网关 + 节点"一体的板子上，ISO-TP 常常同时存在于两条总线（本板 CAN0 给 ECU，CAN1 给网关）。

## 29.4 工作原理

### 29.4.1 四类帧与逐字节格式

ISO-TP 用 N_PCI 首字节的高 3 位区分帧类型，低 4~5 位带参数。图 29-1 是经典 8 字节帧的逐字节布局。

![图 29-1 ISO-TP 四类帧的逐字节格式](figures/29/fig-29-1-frames.png)

**图 29-1** ：SF 单帧、FF 首帧、CF 连续帧、FC 流控帧的字段位置。注意 SF 的长度塞在首字节低 4 位（最大 7），
所以 8 字节及以上的消息必须用 FF+CF。

| 帧类型 | 首字节 | 关键字段 | 经典帧有效载荷 | CAN FD 有效载荷 |
|---|---|---|---|---|
| SF 单帧 | 0x0L | SF_DL=1..7；FD 下 DL>7 时首字节 0x00、次字节为 DL | 1~7 B | 1~62 B |
| FF 首帧 | 0x1H + 0xLL | FF_DL 12 位（0x0LLL）；FF_DL=0 时后随 4 B 长度 | 6 B | 62 B（或 58 B 带转义） |
| CF 连续帧 | 0x2N | SN=1..15 循环，跳过 0 | 7 B | 63 B |
| FC 流控帧 | 0x3F + BS + STmin | FS=0 CTS / 1 WAIT / 2 OVFLW | 无载荷 | 无载荷 |

**表 29-1** ：四类帧的字段与有效载荷。帧数与长度满足：`N_CF = ceil((DL - 首帧载荷) / 每帧载荷)`。

### 29.4.2 发送与接收状态机

本仓库的实现是**全非阻塞状态机**：`app_isotp_send()` 只负责发出 SF 或 FF 并进入等待，
真正的 CF 发送、FC 发送、超时判定都在主循环调用的 `app_isotp_poll()` 里推进。
这样一颗 MCU 可以同时当 DUT 和测试节点，两边在同一个循环里"平铺"调度，不会出现
"tester 在阻塞发送里等 DUT 应答、而 DUT 应答又要等 tester 回 FC"的重入死锁。

![图 29-2 ISO-TP 发送/接收状态机](figures/29/fig-29-2-fsm.png)

**图 29-2** ：左半是发送方（等 FC、按 BS/STmin 发 CF），右半是接收方（发 FC、校验 SN、组包）。

关键字段与状态迁移的代码骨架（摘自 `components/app_isotp/src/app_isotp.c`）：

```c
/* 收到流控帧：决定接下来怎么发 CF */
static void tp_tx_on_fc(app_isotp_t *t, const uint8_t *pci, uint64_t now)
{
    uint8_t fs = pci[0] & 0x0F;      /* CTS / WAIT / OVFLW */
    if (fs == FS_CTS) {
        t->tx_stmin_us = app_isotp_stmin_to_us(pci[2]);
        t->tx_bs_left  = pci[1] ? pci[1] : 0xFFFFu;   /* BS=0 表示不分块 */
        t->tx_next_us  = now;
        t->tx_state    = TX_SEND_CF;
    } else if (fs == FS_WAIT) {
        if (++t->tx_wft > t->cfg.wftmax) { t->tx_state = TX_IDLE; }  /* 超 N_WFTmax */
        t->tx_tmr_us = now;                              /* WAIT 重置 N_Bs */
    } else {
        t->st.err_ovflw++;  t->tx_state = TX_IDLE;       /* FC.OVFLW：对端缓冲不够 */
    }
}
```

### 29.4.3 流控：BS 与 STmin

接收方用 FC 帧告诉发送方两件事：**一次能连发几帧（BS）**、**两帧之间至少隔多久（STmin）**。

BS 与 STmin 的编码规则见表 29-2。

| 字段 | 编码 | 含义 | 本实现默认 |
|---|---|---|---|
| BS | 0x00 | 不分块，后面所有 CF 一次发完 | 0（点对点诊断最快） |
| BS | 0x01~0xFF | 每发 N 个 CF 等一个 FC | 可配 |
| STmin | 0x00~0x7F | 0~127 ms | 0 |
| STmin | 0xF1~0xF9 | 100~900 µs（步长 100 µs） | 可配 |
| STmin | 其它 | 保留值，按 127 ms 处理 | — |

**表 29-2** ：BS 与 STmin 编码。STmin 是**下限**：总线上本来就要花的时间不会被它缩短（30.7 节有实测）。

### 29.4.4 时间参数

四个定时器的定义、默认值与超时后果见表 29-3。

| 参数 | 谁计时 | 触发点 | 超时后果 | 本实现默认 |
|---|---|---|---|---|
| N_As | 发送方 | 请求发一帧 → 帧真的上了总线 | 本端报错，中止 | 折进链路层同步发送 |
| N_Ar | 接收方 | 请求发 FC → FC 上了总线 | 本端报错，中止 | 折进链路层同步发送 |
| N_Bs | 发送方 | 发完 FF 或一个 CF 块 → 收到 FC | 中止发送，对端 N_Cr 超时后复位 | 1000 ms |
| N_Cr | 接收方 | 收到 FF/CF → 收到下一个 CF | 组包失败，丢弃 | 1000 ms |
| N_WFTmax | 发送方 | 连续收到 FC.WAIT 的次数 | 中止发送 | 10 |

**表 29-3** ：ISO-TP 的四个定时器。`"N_" 前缀是 ISO 15765-2 的命名，不是"纳秒"`。

### 29.4.5 经典 CAN 与 CAN FD 的差异

两种物理层的帧数对比见表 29-4。

| 维度 | 经典 CAN | CAN FD（ISO-TP 2016） |
|---|---|---|
| 单帧最大载荷 | 7 B | 62 B（SF_DL>7 用 0x00+次字节转义） |
| 首帧载荷 | 6 B | 62 B |
| 连续帧载荷 | 7 B | 63 B |
| 帧长 | 固定 8 B | 8/12/16/20/24/32/48/64 B，必须用合法 DLC |
| 流控帧 | 8 B（3 B 有效 + 填充） | 可 8~64 B，FC.CT 允许一次带更多信息 |
| BRS | 无 | 仲裁段 1 Mbit/s、数据段 2 Mbit/s |
| STmin=0 语义 | 尽快发 | 尽快发，但下限是"一帧的传输时间"（数据段更短） |
| 4095 字节消息帧数 | 1 + 585 = 586 帧 | 1 + 65 = 66 帧 |

**表 29-4** ：同一份 4095 B 数据在两种物理层下的帧数。FD 的 8.9 倍帧数优势来自"每帧 63 B vs 7 B"。

### 29.4.6 地址模式与 padding

- **正常寻址**：11 位或 29 位 CAN ID，收发用一对 ID（本板 0x7E0/0x7E8、0x700/0x708）。
- **地址扩展（AE）**：数据首字节是目标地址 N_TA，报文长度减 1。部分 OEM 用它在同一对 ID 上区分多个逻辑节点。
- **padding**：经典帧必须 8 B，不足补 0x00/0xCC/0xAA；FD 帧必须落在合法 DLC 上。
  填充字节对协议无意义，但**抓包工具、某些 ECU 会校验**，生产环境要按 OEM 规定填。

## 29.5 寄存器与驱动 API 对照

ISO-TP 本身是纯软件协议，但每一帧最终都要落到 MCAN 的寄存器上。下表把"协议字段 → MCAN 寄存器/机制 → 本仓库 API → 代码位置"串起来。

协议字段与 MCAN 驱动 API 的对应关系见表 29-5。

| ISO-TP 字段 / 行为 | MCAN 侧机制 | 本仓库 API | 代码位置 |
|---|---|---|---|
| 发送一帧 SF/FF/CF/FC | TX Buffer Element（TXBTIE/TXBRP） | `app_isotp_port_send()` | `components/app_isotp/src/app_isotp_port.c` |
| 帧长度（DLC） | MCAN_TXBUF 的 DLC 字段（编码而非字节数） | `tp_len_to_dlc()` | 同上 |
| 接收帧 | RX FIFO0（RXF0S/RXF0A） | `app_isotp_port_recv()` | 同上 |
| 11 位 / 29 位 ID | TXBUF 的 XTD 位 + 标准/扩展滤波器 | `app_isotp_frame_t.ext` | 同上 |
| CAN FD | CCCR.FDOE/BRSE、TXBUF 的 FDF/BRS 位 | `cfg.fd / cfg.brs` | `app_isotp.c` |
| 位时序 | NBTP / DBTP | `tp_bit_timing()` | `app_isotp_port.c` |
| BRS 数据段 2 Mbit/s | DBTP + TDC（SSP） | 同上 | 同上 |
| 帧时间戳 / 超时基准 | MCHTMR（24 MHz） | `app_isotp_port_now_us()` | 同上 |
| Message RAM | AHB SRAM 里的 MCAN 消息缓冲（640 words/节点） | `ATTR_PLACE_AT(".ahb_sram")` | 同上 |
| 收发计数 / 错误 | PSR（LEC）、ECR（TEC/REC）、IR | `app_isotp_stats_t` + `app_isotp_port_t` 计数 | `app_isotp.c` |

**表 29-5** ：协议字段与 MCAN/驱动 API 对照。

## 29.6 例程：samples/app/isotp_echo

例程速览见表 29-6。

| 项 | 内容 |
|---|---|
| 路径 | `samples/app/isotp_echo/` |
| 角色 | CAN0 = DUT（收 0x700 / 发 0x708），CAN1 = 板内 tester（反向） |
| 覆盖特性 | SF/FF/CF/FC、BS/STmin 扫描、padding、29 位 ID、CAN FD、1~4095 B、CRC32 校验 |
| 关键配置 | 经典 500 kbit/s（presc=1 seg1=139 seg2=20，SP=87.5%）；FD 仲裁 1 Mbit/s / 数据 2 Mbit/s，TDC SSP=15 |
| 实测现象 | 8 种长度全部 CRC32 一致；经典 4095 B 往返 273.5 ms；FD 42.2 ms |
| 日志 | `logs/20261030-165808-isotp_echo.log` |

**表 29-6** ：isotp_echo 例程速览。

命令（Letter Shell）：

| 命令 | 作用 |
|---|---|
| `run` | 1/7/8/12/64/256/1024/4095 B 回显 + CRC32 + RTT |
| `bench` | BS=0/8/32 × STmin=0/1 ms/100 µs 共 9 组扫描 |
| `thr [n]` | 4095 B 连续回显 n 次，测稳定吞吐 |
| `fd` / `ext` | 切 CAN FD / 29 位 ID 后复测 |
| `pad [b]` | padding 字节验证（打印尾部字节） |
| `stat` / `info` | 统计 / 当前配置 |

核心逻辑只有 20 行：DUT 收到完整消息就原样回显，tester 发一条等一条。

```c
static void on_srv_msg(void *user, const uint8_t *data, uint32_t len)
{
    /* DUT 侧：收到完整 ISO-TP 消息，立刻把它发回去（非阻塞，几微秒后返回） */
    (void) user;
    if (app_isotp_send(&s_srv, data, len) != APP_ISOTP_OK) {
        s_echo_busy_err++;
    }
}

static void pump_once(void)
{
    app_isotp_frame_t f;
    while (app_isotp_port_recv(&s_port0, &f)) {     /* CAN0 -> DUT 栈 */
        (void) app_isotp_on_frame(&s_srv, &f);
    }
    while (app_isotp_port_recv(&s_port1, &f)) {     /* CAN1 -> tester 栈 */
        (void) app_isotp_on_frame(&s_cli, &f);
    }
    app_isotp_poll(&s_srv);                          /* 推进 CF/FC/超时 */
    app_isotp_poll(&s_cli);
}
```

## 29.7 上板实测

测试台：Bench A（板内双节点），CAN0 与 CAN1 经 TCAN1044AV 挂同一段 120 Ω 终端总线。
测量方法：DUT 的 ISO-TP 从"发出首帧"到"收到完整响应并回显完"用 MCHTMR 计时，单位微秒。

### 29.7.1 经典帧 500 kbit/s（BS=0、STmin=0、pad=0xCC）

经典帧实测数据见表 29-7。

| 长度 B | 回显帧数 | RTT µs | 双向吞吐 KB/s | DUT 发送耗时 µs | CRC32 |
|---|---|---|---|---|---|
| 1 | 1 | 519 | 3.8 | 228 | OK |
| 7 | 1 | 461 | 29.7 | 227 | OK |
| 8 | 2 | 1430 | 10.9 | 701 | OK |
| 12 | 2 | 1408 | 16.6 | 698 | OK |
| 64 | 10 | 5140 | 24.3 | 2556 | OK |
| 256 | 37 | 17729 | 28.2 | 8814 | OK |
| 1024 | 147 | 69034 | 29.0 | 34358 | OK |
| 4095 | 586 | 273504 | 29.2 | 136099 | OK |

**表 29-7** ：经典帧实测（`logs/20261030-165808-isotp_echo.log` 的 `run` 段）。8/8 PASS，seq_err=0、ovflw=0、timeout=0。

### 29.7.2 CAN FD 1 Mbit/s / 2 Mbit/s

CAN FD 实测数据见表 29-8。

| 长度 B | 回显帧数 | RTT µs | 双向吞吐 KB/s | 相对经典 |
|---|---|---|---|---|
| 1 | 1 | 170 | 11.5 | — |
| 7 | 1 | 167 | 81.9 | — |
| 8 | 1 | 209 | 74.8 | 8 B 只占 1 帧 |
| 12 | 1 | 240 | 97.7 | 1 帧 vs 经典 2 帧 |
| 64 | 2 | 982 | 127.3 | 5.2× |
| 256 | 5 | 2913 | 171.6 | 6.1× |
| 1024 | 17 | 10756 | 185.9 | 6.4× |
| 4095 | 66 | 42197 | 189.5 | **6.5×** |
| 6000 | 96 | 61585 | 190.3 | 走 32 位长度转义（1 FF + 95 CF） |

**表 29-8** ：CAN FD 实测（`logs/20261102-120902-isotp_echo.log` 的 `fd` 段，2026-11-02 复测）。
29 位 ID（0x18DAF110/0x18DA10F1）下 4095 B 为 44598 µs、179.3 KB/s、8/8 PASS。6000 B 是 30.7.6 节的新增用例。

### 29.7.3 BS / STmin 扫描（经典 500 kbit/s，1024 B）

BS/STmin 扫描结果见表 29-9。

| BS | STmin=0 | STmin=1 ms | STmin=100 µs |
|---|---|---|---|
| 0 | 69031 µs / 29.0 KB/s | 291422 µs / 6.9 KB/s | 69031 µs / 29.0 KB/s |
| 8 | 77391 µs / 25.8 KB/s | 272172 µs / 7.3 KB/s | 77237 µs / 25.9 KB/s |
| 32 | 70880 µs / 28.2 KB/s | 287151 µs / 7.0 KB/s | 70898 µs / 28.2 KB/s |

**表 29-9** ：27 组 BS × STmin × 长度全部 PASS（`logs/20261030-165657-isotp_echo.log`）。

三个结论：

1. **STmin=1 ms 把吞吐压到 1/4**。1024 B 要 147 帧，147×1 ms = 147 ms 单向，与实测 291 ms 往返吻合，
   说明 STmin 计时准确到毫秒级。
2. **STmin=100 µs 与 STmin=0 完全一样**：每帧在 500 kbit/s 上本来就要 176~216 µs，100 µs 的下限被物理层吸收。
3. **BS=8/32 比 BS=0 慢 5~12%**：每块结束多一次 FC 往返（约 230 µs）加软件处理。

### 29.7.4 padding 上总线的证据

`pad 170` 把填充字节设为 0xAA，然后跑 64 B 消息（1 FF + 9 CF），最后一个 CF 只有 2 B 有效载荷：

```text
[isotp_echo] pad test: pad=0xAA last_frame_len=8 tail=AA AA AA AA AA -> PASS(经典帧固定 8 字节)
```

尾部 5 字节确实是 0xAA，说明 padding 真的写进了 CAN 帧，不是软件里"看起来对"。

### 29.7.5 相邻 CF 间隔实测（U-30-2）

STmin=0 的含义是"接收方不额外要求间隔"，发送方能多快发就多快发。为了拿到这个"多快"，
在协议栈里对每个 CF 的发送起始时刻打点（`mchtmr` 24 MHz 微秒时基），统计相邻 CF 的间隔。

| 模式 | CF 载荷 | 间隔样本数 | 平均 | 最小 | 最大 | 单帧线上时间（估算） |
|---|---|---|---|---|---|---|
| 经典 500 kbit/s | 7 B | 584 | 231 µs | 224 µs | 246 µs | 约 222 µs |
| CAN FD 1 M/2 M | 63 B | 64 | 320 µs | 312 µs | 338 µs | 约 300 µs |

**表 29-10** ：STmin=0 时相邻 CF 间隔（4095 B 回显，`stat` 命令的 `cf gap` 行）。

读法：间隔几乎等于**单帧在总线上的时间**，软件开销只占 9~20 µs。也就是说 STmin=0 在本实现里
确实是"背靠背"，没有被软件节流。FD 下单帧 320 µs 反而比经典 231 µs 长，但每帧多装 56 B 载荷，
按"每字节时间"算 FD 是经典的 6.5 倍。

![图 29-3 STmin=0 时相邻 CF 间隔实测](figures/29/fig-29-3-cf-gap.png)

**图 29-3** ：左图是实测平均间隔与单帧线上时间（误差线为 min~max），右图按每帧载荷折算的单向速率。
数据来自 `logs/20261102-120902-isotp_echo.log`，脚本 `tools/figs/app30_cf_gap.py`。

软件侧还有一个限制：`APP_ISOTP_DEFAULT_BURST=4`，一次 poll 最多连发 4 个 CF，然后回主循环。
所以上面的间隔里已经包含"每 4 帧一次主循环"的成本。想再压，就把 burst 调大或把 CF 发送放中断，
代价是接收侧 RX FIFO 更容易溢出（见 30.9 易错点 2）。

### 29.7.6 >4095 B 的 32 位长度转义与 200 次高负载（U-30-5 / U-30-6）

ISO 15765-2:2016 里 FF_DL 的 12 位只够 4095 B；超过就要 FF 前两字节写 0x10 0x00，后面跟 4 B 大端长度。
本实现两条路径都有（发送 `tp_start_ff()`、接收 `PCI_FF` 分支），但之前一直没跑过 >4095 B 的用例。

这次把 `TP_MAX` 提到 8192、FD 用例表加 6000 B，板内双节点实测：

```text
[isotp_echo] run: CANFD 1M/2M 11bit BS=0 STmin=0x00 pad=0xCC
  6000   96      61585     190.3     30665    320      OK
[isotp_echo] PASS sizes=9/9 bytes=22934 rtt_avg=13248 us rtt_max=61585 us ... ff_ext=1
```

`ff_ext=1` 是发送侧对转义 FF 的计数，96 帧 = 1 FF + 95 CF（首帧装 57 B，其余每帧 63 B），
CRC32 校验一致，说明 32 位长度字段的字节序和接收侧解析都对。

200 次 4095 B 经典回显（每次 586 帧单向，共 117200 帧发送）：

```text
[isotp_echo] thr 4095B x200 ok=200 total=54857041 us => 29.2 KB/s payload(双向) 3.6 msg/s
[isotp_echo] DUT stats: txmsg=200 rxmsg=200 txf=117200 rxf=117400 fc_tx=200 fc_rx=200 wait=0
                         seq=0 ovflw=0 timeout=0 link=0
  cf gap: cnt=584 avg=231 us min=224 us max=246 us | ff_ext=0
```

54.9 s 里 200 条消息、0 序号错、0 溢出、0 超时、`echo_busy=0`（DUT 回显时从没遇到忙），
吞吐稳定在 29.2 KB/s，和单次测量的 29.2 KB/s 一致。结论：本实现没有随运行时间累积的
状态泄漏或计数器回绕问题（至少 200 条 / 11.7 万帧量级）。

### 29.7.7 与主机 ISO-TP 工具互通（U-30-1 遗留）

本章所有数字都是板内 CAN0↔CAN1 双节点（Bench A），不经过 cannelloni 网关。经网关的路径
（Bench B，vcan0）会多出约 2.3 ms 的 UDP 往返，1064 B 以上的消息还会碰到 cannelloni UDP
fire-and-forget 丢包（见第 33 章 U-33-11），本轮没有独立窗口复测，作为已知限制留给下一轮。

## 29.8 性能与调优

![图 29-4 实测 RTT 与吞吐 vs 消息长度](figures/29/fig-29-4-perf.png)

**图 29-4** ：左图是回显 RTT（对数坐标），右图是双向有效载荷吞吐。两条曲线在 256 B 之后收敛，
说明小消息受**软件/往返开销**限制，大消息受**总线物理带宽**限制。

理论上限与实测对比（经典 500 kbit/s，每帧 8 B 数据 + 约 44 bit 开销 ≈ 108 bit/帧）：

理论值与实测值的达成率见表 29-11。

| 指标 | 理论值 | 实测 | 达成率 |
|---|---|---|---|
| 单帧时间 | 108 bit / 500 kbit/s = 216 µs | 约 227 µs | 95% |
| 4095 B 单向（586 帧） | 126.5 ms | 136.1 ms | 93% |
| 4095 B 往返（1172 帧） | 253 ms | 273.5 ms | 92% |
| 经典有效载荷带宽（双向） | 31.1 KB/s | 29.2 KB/s | 94% |
| CAN FD 有效载荷带宽（双向） | 约 210 KB/s | 189.6 KB/s | 90% |

**表 29-11** ：性能达成率。缺口来自帧间隔、软件轮询延迟、以及每块一次 FC 的往返。

调优建议（按收益排序）：

1. **能上 FD 就上 FD**：同一份 4095 B 数据 RTT 从 273.5 ms 降到 42.2 ms（6.5 倍），这是物理层收益，不需要改协议逻辑。
2. **BS=0 优先**：点对点诊断链路里 BS=0 最快；只有在接收方 RAM 紧张（比如多通道并发）时才用 BS=8/16 限流。
3. **STmin=0 或 F1**：诊断场景 SC 通常给 0；给 1 ms 是自缚手脚（吞吐直接 1/4）。
4. **一次 poll 的 CF 突发要限流**：本实现 `APP_ISOTP_DEFAULT_BURST=4`。FD 的 MCAN RX FIFO0 只有 8 个元素，
   不限流会溢出（30.9 易错点 2）。
5. **减少软件层拷贝**：接收侧目前是"组包到 rx_buf 再交上层"，刷写场景可以改成"边收边写 Flash"省一次 32 KB 拷贝。

## 29.9 易错点

1. **MCAN 的 DLC 是编码不是字节数**。`tx.dlc = 16` 不是"16 字节"，而是 DLC=16（非法）。
   经典 8 字节恰好 dlc==字节数，所以只跑经典帧时完全看不出问题；一开 FD，12/64/256/1024/4095 全部超时。
   证据：`logs/20261030-165657-isotp_echo.log` 里 FD 段的 FAIL 行（修复前）。12 B 对应 DLC=9，64 B 对应 DLC=15。
2. **FD 的 RX FIFO 只有 8 个元素**。经典默认 32 个、FD 默认 8 个（`MCAN_RXFIFO_SIZE_CANFD_DEFAULT`）。
   如果发送方一次 poll 把几百个 CF 全灌进 TX FIFO，接收方还没轮到执行就溢出丢帧，
   表现是"偶发 SN mismatch / N_Cr 超时"。
3. **板内双节点不能用嵌套阻塞**。tester 阻塞发送 → pump 里回调 DUT 应答 → DUT 应答又要等 tester 的 FC，
   而 tester 正卡在发送里 → 双方 N_Bs 超时。必须把 ISO-TP 做成全非阻塞状态机，在主循环里平铺调度。
4. **padding 字节不是随便填**。经典帧固定 8 B，不填充也能发（DLC=实际长度），但不少 OEM 的 ECU 与抓包脚本
   假定固定 8 B；FD 帧必须落在合法 DLC 上，否则 MCAN 直接拒绝发送。
5. **FF_DL=0 是转义，不是"长度 0"**。消息长度 >4095 B 时 FF 前两字节是 0x10 0x00，后跟 4 B 大端长度。
   把 0 当长度会直接判为非法消息。
6. **SN 从 1 开始且跳过 0**。收到 SN=0 或跳号必须丢弃整条消息（本实现计数 `err_seq`），
   不能"容错接着拼"——那会把两段不同的数据拼成一条。
7. **FC.WAIT 要重置 N_Bs**。接收方忙不过来时可以连续发 WAIT，但**每次 WAIT 都重置发送方的 N_Bs**，
   否则发送方会在对端还在处理时就超时。本实现用 `tx_wft` 计 N_WFTmax 防死等。
8. **串口采集比上电晚**。hwrun 在烧写后 0.3 s 才开始抓串口，上电立刻 `printf` 的自测输出会丢；
   例程里 `board_delay_ms(1500)` 之后再自测。

## 29.10 对比：ISO-TP、DoIP 与裸 CAN

三种传输方式的对比见表 29-12。

| 维度 | ISO-TP over CAN | DoIP（ISO 13400） | 裸 CAN 帧 |
|---|---|---|---|
| 物理层 | CAN / CAN FD | 100BASE-TX / 千兆以太网 | CAN / CAN FD |
| 单条消息上限 | 4095 B（经典）/ 8192 B（本实现） | 受 TCP/UDP 与缓冲限制，诊断常用 64 KB | 8 B / 64 B |
| 流控 | 由 ISO-TP 的 FC 帧做 | 由 TCP 窗口做 | 无 |
| 典型 RTT | 0.4~3 ms（小消息）/ 273 ms（4095 B） | 0.2~1 ms（同网段） | 0.2 ms |
| 有效带宽 | 29 KB/s（经典）/ 190 KB/s（FD，实测） | 数十 MB/s | 54 KB/s（经典理论） |
| 适用场景 | 车内 ECU 诊断 / 刷写 / OBD | 整车以太网诊断、大数据量刷写 | 传感器/控制信号 |
| 本板实测 | 本章 | 见第 28 章（SOME/IP 与网关） | 第 8/9 章 |

**表 29-12** ：三种传输方式对比。刷写 32 KB 用经典 ISO-TP 约 1.3 s，用 DoIP 可以到几十毫秒，
但车内布线成本、ECU 侧以太网 PHY 成本完全不同——这也是为什么"网关 + DoIP + CAN"三层架构仍是主流。

## 29.11 参考来源

上游与官方链接（核对日期 2026-11-02）：

- ISO 15765-2:2016 <https://www.iso.org/standard/66574.html>（标准正文收费，页面可看范围与摘要）
- ISO 11898-1:2015 <https://www.iso.org/standard/63648.html>（CAN FD 帧格式与位时序）
- Linux 内核 ISO-TP 实现 <https://github.com/torvalds/linux/blob/master/net/can/isotp.c>；SocketCAN 文档 <https://www.kernel.org/doc/html/latest/networking/can.html>
- can-utils（isotpsend/isotprecv/isotpdump）<https://github.com/linux-can/can-utils>
- python-can 文档 <https://python-can.readthedocs.io/>
- HPM5E00 产品与文档入口 <https://www.hpmicro.com/>、<https://doc.hpmicro.com/>；SDK <https://github.com/hpmicro/hpm_sdk>


1. ISO 15765-2:2016 *Road vehicles — Diagnostic communication over Controller Area Network (DoCAN) — Part 2: Transport protocol and network layer services*。
2. ISO 11898-1:2015 *CAN data link layer and physical coding sub-layer*（CAN FD 帧格式与位时序）。
3. Linux 内核 `net/can/isotp.c` 与 can-utils 的 `isotpsend`/`isotprecv`/`isotpdump`，参考代码在 `can-utils/`。
4. HPM5E00 用户手册 MCAN 章节：TX/RX Buffer、Message RAM、位时序与 TDC 寄存器（`RM §MCAN`）。
5. 本仓库第 08 章（CAN FD 节点）、第 09 章（CAN 性能）、第 30 章（UDS）。
6. 实测日志：`logs/20261030-165808-isotp_echo.log`、`logs/20261030-165657-isotp_echo.log`、
   `logs/20261102-120902-isotp_echo.log`（6000 B 转义 + 200 次高负载 + CF 间隔）。
7. 图脚本：`tools/figs/app30_isotp_frames.py`、`tools/figs/app30_isotp_fsm.py`、
   `tools/figs/app30_isotp_perf.py`、`tools/figs/app30_cf_gap.py`。

## 29.12 待确认项与风险

表中条目已逐条复核（2026-11-02）：能上板测的给了数据和日志路径；本轮确实测不了的降级为已知限制，写明原因和一条可执行的验证方法，不再保留待确认/未尝试的裸条目。


| 编号 | 项 | 状态 | 验证方法 |
|---|---|---|---|
| U-30-1 | 上位机（vcan0 经 CAN 网关）路径的 ISO-TP 实测数字 | 已知限制（本轮无 Bench B 窗口） | 原因：板卡窗口都给了板内双节点用例，网关路径要占 cannelloni + vcan 且会和别人的测试互相复位。验证方法：板端跑 `isotp_echo`，主机 `python3 host/uds/isotp_test.py --channel vcan0`，对比 30.7.1 的 RTT（网关应多约 2.3 ms） |
| U-30-2 | CAN FD 下 STmin=0 的发送方最小间隔 | **已实测（2026-11-02）**：经典 231 µs（224~246）、FD 320 µs（312~338），间隔≈单帧线上时间，软件开销 9~20 µs | 见 30.7.5、表 29-10；日志 `logs/20261102-120902-isotp_echo.log`；参见 图 29-3 |
| U-30-3 | FC.CT（流控帧 64 B）带额外信息的兼容性 | 已知限制（无第三方 ECU） | 原因：板上只有两节点互通，本实现 FC 只用 3 B 有效字段、按 DLC 填充。验证方法：接量产诊断仪或主机 `isotpsend/isotprecv` 抓 FC 帧，确认对端接受 8 B/64 B padding 的 FC.CT |
| U-30-4 | 1 字节地址扩展（AE）与真实 OEM 诊断仪的互通 | 已知限制（无 OEM 诊断仪） | 原因：AE 需要两侧 N_TA/N_SA 配置一致，板上没有第二个支持 AE 的实现。验证方法：`isotp_echo` 里把 `cfg.ext_addr_en=true` 并给两侧配同一对地址，抓帧看扩展地址是否在 SF/FF/CF/FC 前都只出现一次 |
| U-30-5 | 4095 B 以上的 32 位长度转义路径 | **已实测（2026-11-02）**：6000 B 在 FD 下 PASS，96 帧（1 FF + 95 CF），RTT 61.6 ms、190.3 KB/s、CRC32 一致，`ff_ext=1` | 见 30.7.6；日志 `logs/20261102-120902-isotp_echo.log` |
| U-30-6 | 长时间高负载下的丢帧率 | **已实测（2026-11-02）**：200 次 4095 B 回显 200/200 PASS，54.86 s、117200 帧、seq=0/ovflw=0/timeout=0、echo_busy=0 | 见 30.7.6；日志 `logs/20261102-120902-isotp_echo.log` |
