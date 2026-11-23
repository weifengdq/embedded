# host/xcp —— XCP 上位机工具（pyxcp + python-can）

Bench B 链路（见 \`docs/应用层开发约定.md\` §3）：

    host vcan0 <--UDP(cannelloni)--> board ENET <--> MCU CAN1 (gateway) <--> bus <--> MCU CAN0 (XCP slave)

板端固件：\`samples/app/xcp_can_daq\`（或 \`xcp_can_cal\`），shell 里先 \`gw on\` 打开 cannelloni 网关。

## 依赖

- Python：\`$HOME/pyenv/bin/python\`（已装 **pyxcp 0.30.1**、**python-can 4.6.1**）；
- 网关：\`host/can_bridge/\`（vcan0 + stock cannelloni），见该目录 README；
- A2L：\`samples/app/xcp_can_daq/xcp_can_daq.a2l\`（由 \`tools/gen_a2l.py\` 从 ELF 生成，含变量绝对地址）。

## 文件

| 文件 | 作用 |
|---|---|
| \`xcp_common.py\` | A2L 地址解析（MEASUREMENT/CHARACTERISTIC → 绝对地址/类型）+ pyxcp Master 构造 |
| \`xcp_daq.py\` | 连从站、配动态 DAQ 列表（3 ODT + 时间戳）、采集 N 秒，输出采样率/丢帧/时间戳抖动 + CSV |
| \`xcp_cal.py\` | 在线读/写/回读标定量、MODIFY_BITS、GET_SEED/UNLOCK、Kp 扫参 |
| \`run_daq.sh\` / \`run_cal.sh\` | 一键脚本（默认用仓库内 A2L，走 vcan0） |
| \`xcp_eth_client.py\` | 自写 XCPonEth(UDP) 客户端：4 B LEN/CTR 头、CTR 丢包统计、幂等命令超时重发 |
| \`run_eth.sh\` | XCPonEth 一键脚本（Bench C 直连 192.168.2.100:5555，ping 预检 + DAQ） |
| \`run_can_regress.sh\` | CAN 路径回归：\`xcp_daq_raw.py\`（自写）+ \`xcp_daq.py\`（pyxcp，记录 U-35-2 状态） |

## 用法

~~~bash
# 0) 起 vcan0 + cannelloni（在 host/can_bridge 里，需要 sudo）
cd host/can_bridge && sudo ./vcan_setup.sh && ./build_cannelloni.sh && ./run_tunnel.sh &

# 1) DAQ 测量
$HOME/pyenv/bin/python host/xcp/xcp_daq_raw.py \
    --a2l samples/app/xcp_can_daq/xcp_can_daq.a2l --channel vcan0 --secs 6 --csv out/xcp_daq.csv

# 2) 在线标定
$HOME/pyenv/bin/python host/xcp/xcp_cal.py \
    --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l --channel vcan0 --sweep 0.1:2.0:10
~~~

配合板锁（一条命令完成"烧写+抓串口+上位机测试"）：

~~~bash
bash scripts/hwrun.sh app/xcp_can_daq --secs 15 --send "gw on" \
     --post "timeout 70 bash host/xcp/run_daq.sh" --lock-wait 1800
~~~

## XCPonEth（\`--interface eth\`，Bench C 直连）

不需要 cannelloni、不需要 vcan：主机 \`enp6s0\` 192.168.2.1 直连板 192.168.2.100:5555。
板端固件 \`samples/app/xcp_eth\`，A2L 用 \`samples/app/xcp_eth/xcp_eth.a2l\`（由
\`samples/app/xcp_eth/gen_a2l_eth.py\` 从 ELF 生成）。

~~~bash
# rate 档：EVT0 1 kHz / 4 ODT / 时间戳
$HOME/pyenv/bin/python host/xcp/xcp_daq.py \\
    --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --secs 8 \\
    --csv out/xcp_eth_rate.csv

# stress 档：EVT1 10 kHz / 8 ODT（单包多 ODT 与带宽上限）
$HOME/pyenv/bin/python host/xcp/xcp_daq.py \\
    --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --plan stress --secs 6

# 一键（带 ping 预检）
bash host/xcp/run_eth.sh 8
XCP_PLAN=stress bash host/xcp/run_eth.sh 6
~~~

\`--interface eth\` 时脚本做四件事：CONNECT/GET_STATUS/GET_ID/GET_DAQ_CLOCK → 标定写读回
（SHORT_UPLOAD/SET_MTA+DOWNLOAD/MODIFY_BITS）→ 200 次 SHORT_UPLOAD 的 RTT 统计 → 动态 DAQ 采集，
最后读回板端统计镜像（\`g_stat_*\`）做"板端没发 vs 链路丢了"的交叉验证。

实测（2026-11-02，日志 \`logs/20261102-155848-host-xcp-eth-*.log\`）：

| 档位 | 事件率 | 消息率 | 包率 | 载荷吞吐 | 丢包 | RTT p50 |
|---|---|---|---|---|---|---|
| rate（1 kHz/4 ODT/6 s） | 990.0 Hz | 3960 msg/s | 990 pkt/s | 0.42 Mbit/s | 1.25% | 109.9 µs |
| stress（10 kHz/8 ODT/6 s） | 9723.9 Hz | 77791 msg/s | 9724 pkt/s | 6.85 Mbit/s | 3.09% | 见日志 |
| long30（1 kHz/4 ODT/30 s） | 993.0 Hz | 3972 msg/s | 993 pkt/s | 0.42 Mbit/s | 0.87% | 见日志 |

板端三档 \`tx_drop / daq_dropped / overload\` 全 0，帧错误 0 —— **丢包是 UDP 链路属性**，不是从站丢帧。
注意：\`--interface eth\` 与 CAN 的 \`--interface socketcan\` 共用 \`--a2l/--secs/--csv\`，
CAN 路径参数不变；默认（不带 \`--interface eth\`）仍是 pyxcp + python-can 的 CAN 路径。

~~~bash
# CAN 路径不回归：raw 客户端 PASS，pyxcp 仍复现 U-35-2（上游 bug，与本轮改动无关）
bash scripts/hwrun.sh app/xcp_can_daq --secs 12 --send "gw on" \\
     --post "timeout 150 bash host/xcp/run_can_regress.sh 5" --lock-wait 1800
~~~

## 设计要点

1. **控制面 / 数据面分离**：pyxcp 负责 CTO（CONNECT、DAQ 配置、标定读写）；
   DAQ 的 DTO 由**独立的 python-can socket** 收（SocketCAN 允许多个 socket 收同一总线），
   这样可以拿到内核时间戳做抖动统计，也不受 pyxcp 策略层影响。
2. **地址只来自 A2L**：\`xcp_common.load_a2l()\` 解析 \`ECU_ADDRESS\`；A2L 与 ELF 不一致时读出来的值会明显不对
   （"标定无效"最常见的根因）。
3. **DAQ 打包计划必须与固件一致**（本方向例程：3 ODT + 时间戳，见 \`xcp_daq.py\` 顶部注释）。

## 预期输出（DAQ）

    A2L 地址:
      g_adc_raw        0x01200E54  UWORD        2 B
      ...
    [1] CONNECT vcan0 (CMD=0x555 RES=0x556)
        resource=0x05 MAX_CTO=8 MAX_DTO=8 proto=1.4 trans=0.1
        GET_STATUS session=0x0000 protection=0x00
    [2] DAQ 配置: 1 列表 / 3 ODT / 时间戳 on
        first PID = 0 (PID = 绝对 ODT 号)
    [3] 采集 10.0 s ...
    [4] 统计
      实测事件率(Hz)         999.0
      事件丢失(按 tick 计数) 0
      时间戳间隔 mean(us)    1000.0
    DAQ PASS (lost=0 rate=999.0 Hz)

> 联调状态：CAN（Bench B）与 XCPonEth（Bench C）两条路径均已有上板实测日志，见第 34 章
> U-34-2（本章记录时网关刚打通，10 s 采集尚未跑完，不写未测数字）。
