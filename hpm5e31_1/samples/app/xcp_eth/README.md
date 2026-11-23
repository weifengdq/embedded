# samples/app/xcp_eth —— XCP on UDP（XCPonEth）从站 + DAQ 测量

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

第 34 章例程 3：板端跑 **XCPonEth** 从站（UDP 5555），主机 `host/xcp/xcp_daq.py --interface eth` 直连，
完成 CONNECT → 标定写读回 → 动态 DAQ（1 kHz/4 ODT、10 kHz/8 ODT）→ 30 s 长测全流程。
与 `samples/app/xcp_can_daq` 复用同一份协议层 `components/app_xcp/xcp_core.c`，只把传输层换成
`xcp_port_udp.c`（4 B LEN/CTR 头、单包多 ODT、CTR 丢包统计）。

## 硬件接线

不需要飞线、不需要 CAN 网关：YT8011AN 千兆车载以太网口 ↔ 千兆车载以太网盒 ↔ 主机 `enp6s0`。

| 端 | IP/端口 | 说明 |
|---|---|---|
| 板 | 192.168.2.100/24，UDP **5555** | `app_lwip_init()` 静态 IP；XCPonEth 标准端口 |
| 主机 | 192.168.2.1/24（enp6s0） | 已配好，不要改 |

## 构建 / 烧写

~~~bash
cd hpm5e31_1
bash scripts/build.sh samples/app/xcp_eth
# 生成 A2L（地址来自 ELF，必须与烧写镜像一致；IF_DATA 用 XCP_ON_UDP_IP）
python3 samples/app/xcp_eth/gen_a2l_eth.py --elf build/xcp_eth-debug/output/demo.elf \
        --vars samples/app/xcp_eth/xcp_eth_vars.json --out samples/app/xcp_eth/xcp_eth.a2l
# 烧写 + 抓串口（整机锁；不要直连 J-Link）
bash scripts/hwrun.sh app/xcp_eth --secs 15 --send "xcp info;xcp stat" --lock-wait 1800
~~~

上电自动起网，串口会打印一行 **`XCP-ETH INIT PASS ip=192.168.2.100 port=5555`**（hwrun 自证用）。

## shell 命令（letter-shell，921600 8N1）

| 命令 | 作用 |
|---|---|
| `xcp info` | 打印网络/端口/上限/事件通道/命令列表 |
| `xcp stat` | XCP core + UDP 传输层统计 + 各 DAQ 列表状态 + 被测变量 |
| `xcp daq [ms]` | 观察窗口（默认 1000 ms）：打印事件率/消息率/包率/带宽/丢弃 |
| `xcp cal [kp f|ki f|max u16|duty u16|reset]` | 查看/本地改标定页（RAM 单页） |
| `xcp seed <0|1>` | 开关 SEED/KEY（演示 GET_SEED/UNLOCK，key = seed ^ 0xA5） |
| `xcp start` / `xcp stop` | 重新起/停 UDP 服务（stop 会关 socket） |
| `xcp ip` | 打印 netif/PHY 状态（link/speed/PHYSR/GBSR） |

## 被测变量与 A2L

A2L 由 `samples/app/xcp_eth/gen_a2l_eth.py` 从 ELF 符号表生成（复用 `tools/gen_a2l.py`，只把
IF_DATA 的传输层块换成 A2L 1.7.1 的 `XCP_ON_UDP_IP`：PORT 5555 / ADDRESS "192.168.2.100" /
MAX_BUS_LOAD 100 / MAX_BIT_RATE 1000）。

| 变量 | 类型 | 含义 | 事件 |
|---|---|---|---|
| `g_adc_raw` | u16 | ADC0.IN1（PF26）原始值 | EVT0 |
| `g_tick_cnt` | u32 | EVT0（1 kHz）事件计数，主机按连续差判丢帧 | EVT0 |
| `g_tick_10k` | u32 | EVT1（10 kHz）事件计数，压力档丢帧判定 | EVT1 |
| `g_tick_100` | u32 | EVT2（100 Hz）事件计数 | EVT2 |
| `g_pid_mid` | i32 | PID 积分项（定点 1/1000） | EVT0 |
| `g_cpu_load_pm` | u16 | 主循环负载（千分比） | EVT0 |
| `g_u8_cnt` | u8 | EVT0 计数低 8 位 | EVT0 |
| `g_kp/g_ki` | f32 | 标定量（PID 增益） | — |
| `g_ctrl_max_pm` / `g_settle_ms` / `g_pwm_duty_pm` | u16 | 标定页演示量 | — |
| `g_sw_flag` | u32 | MODIFY_BITS 演示 | — |
| `g_stat_*` | u32 ×10 | 板端统计镜像（1 Hz，主机 SHORT_UPLOAD 读回，用于丢包定位） | — |

事件通道：EVT0 1 kHz、EVT1 10 kHz、EVT2 100 Hz（GPTMR0 三通道，1 µs 时基来自 CPU mcycle）。

## 主机侧（Bench C，直连）

~~~bash
# 1 kHz / 4 ODT（默认 rate 档）
$HOME/pyenv/bin/python host/xcp/xcp_daq.py \
    --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --secs 8 \
    --csv out/xcp_eth_rate.csv

# 10 kHz / 8 ODT（stress 档，测单包多 ODT 与带宽上限）
$HOME/pyenv/bin/python host/xcp/xcp_daq.py \
    --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --plan stress --secs 6

# 一键脚本（带 ping 预检）
bash host/xcp/run_eth.sh 8            # rate 档
XCP_PLAN=stress bash host/xcp/run_eth.sh 6
~~~

一次持锁内完成"烧写 + 串口 + 三档测试"：

~~~bash
bash scripts/hwrun.sh app/xcp_eth --secs 8 --send "xcp info;xcp stat" \
     --post "timeout 200 bash host/xcp/xcp_eth_final.sh" --lock-wait 1800
~~~

（`xcp_eth_final.sh` 是临时脚本，内容见第 34 章"复跑命令"；也可以按上面三条命令逐条跑。）

## 实测（2026-11-02）

日志：`logs/20261102-155848-xcp_eth.log`（板端）+ `logs/20261102-155848-host-xcp-eth-rate.log`、
`...-stress.log`、`...-long30.log`（主机三档）。全部同一固件、同一次持锁窗口。

| 指标 | rate：EVT0 1 kHz/4 ODT/6 s | stress：EVT1 10 kHz/8 ODT/6 s | long30：1 kHz/4 ODT/30 s |
|---|---|---|---|
| 主机事件率 | 990.0 Hz | 9723.9 Hz（含丢失后到达率） | 993.0 Hz |
| 板端 tick 速率 | 999.9 Hz | 9999.3 Hz | 1000.0 Hz |
| DAQ 消息率 | 3960 msg/s | **77791 msg/s** | 3972 msg/s |
| UDP 包率 | 990 pkt/s | 9724 pkt/s | 993 pkt/s |
| 单包消息数 | 4.00 | 8.00 | 4.00 |
| 载荷吞吐 | 51.2 KB/s = 0.42 Mbit/s | **835.7 KB/s = 6.85 Mbit/s** | 51.4 KB/s = 0.42 Mbit/s |
| CTR 丢包 / tick 丢帧 | 240 / 60（1.25%） | 13224 / 1653（3.09%） | 840 / 210（0.87%） |
| 帧错误（LEN/截断） | 0 | 0 | 0 |
| 板端 tx_drop / daq_dropped / overload | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| 命令 RTT p50 | 109.9 µs | 见日志 | 见日志 |
| 结论 | **PASS** | **PASS** | **PASS** |

结论要分开说：**板端没有丢帧**（drop/overload 全 0、`daq_frames` 与 tick×ODT 对得上）；
1.25%/3.09%/0.87% 是 UDP 链路丢包（同窗口 ping 0%，属小包突发队列/环形缓冲问题，
见第 34 章"坑 13"与 U-35-9）。XCP 无重传，主机用 CTR 间隙 + `g_tick_*` 连续性双重统计。

标定面同一轮全部 PASS：g_kp 1.200→1.300→1.200、MODIFY_BITS 0x12345678→0x12345A78、
g_pwm_duty_pm 500→300→500。

## 坑

1. **LEN 不含 4 B 头**（任务书写的"含头"是笔误），非最后一条消息的 4 B 对齐填充要计入 LEN，
   否则 pyxcp/CANape 顺序解析会错位（第 34 章坑 9/10）。
2. **UDP 无重传**：CTR 只统计不修复；主机对幂等命令重发，对 UPLOAD/DOWNLOAD/WRITE_DAQ
   （会推进 MTA/条目指针）必须"重设 MTA/DAQ_PTR 再发"。
3. **MTU 1472 B**：`IP_FRAG` 未开，超了直接丢；算 DAQ 包按 `4 + align4(ODT)` × 条数 ≤ 1472。
4. **ENET TX CRC 用 append**（`ENET_TX_HW_CRC_MODE=0`）：replace 模式连续大帧会踩坏相邻 pbuf（第 35 章实测）。
5. **回调上下文**：UDP 收包回调在 `app_lwip_poll()`（主循环）里，不要在回调里 printf/阻塞。
6. **A2L 与 ELF 必须同一次构建**：改代码后要重新生成 A2L，否则 SHORT_UPLOAD 读出来的是别的地址。

## 复跑（最短路径）

~~~bash
bash scripts/hwrun.sh app/xcp_eth --secs 8 --send "xcp info;xcp stat" \
     --post "timeout 120 $HOME/pyenv/bin/python host/xcp/xcp_daq.py \
             --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --secs 6" --lock-wait 1800
~~~
