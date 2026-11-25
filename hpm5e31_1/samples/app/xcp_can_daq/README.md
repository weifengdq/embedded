# samples/app/xcp_can_daq —— XCP on CAN 从站 + DAQ 测量

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

第 33 章例程 1：板端跑 XCP 从站（CAN0），用一个**板内最小 XCP 主机**（CAN1）在无上位机的情况下
把 CONNECT → 内存读写 → DAQ 配置 → 1 kHz 采集全流程跑一遍，得到真实线上数字。

## 硬件接线

不需要任何外部接线/上位机：板上 CAN0（PD00/PD01）与 CAN1（PD05/PD04）经 TCAN1044AV 接在
同一段总线（两端 120 Ω 已接）。CAN0 = XCP 从站（0x555 CMD / 0x556 RES+DAQ），CAN1 = 板内主机。
接上位机时把 CAN1 换成 cannelloni 网关（\`host/can_bridge\`），主机 vcan0 上用 pyxcp 当主机。

## 命令（letter-shell，921600 8N1）

| 命令 | 作用 |
|---|---|
| \`info\` | 打印 CAN/事件/内存区域/DAQ 上限配置 |
| \`selftest [daq_ms]\` | 完整板内自证（默认 800 ms 采集），打印 PASS/FAIL 结论行 |
| \`daq [ms]\` | 只跑 DAQ 配置 + 采集 |
| \`stat\` | XCP/DAQ/CAN 统计 + 各 DAQ 列表状态 |
| \`evstat\` | 每事件打包开销（cycle→µs）与事件计数 |
| \`meas\` | 打印被测变量值及其**绝对地址**（对照 A2L） |
| \`prot <0\|1>\` | 开关种子保护（演示 GET_SEED/UNLOCK） |
| \`clr\` | 清统计 |

上电自动跑一遍 \`selftest 600\`，串口最后一行是 \`XCP SELFTEST PASS fail=0\`（供 hwrun 抓取）。

## 被测变量（A2L 由 tools/gen_a2l.py 从 ELF 生成）

| 变量 | 类型 | 含义 | 采集事件 |
|---|---|---|---|
| \`g_adc_raw\` | u16 | ADC0.IN1（PF26）原始值 | EVT0 1 kHz |
| \`g_tick_cnt\` | u32 | 1 kHz 事件计数（主机按连续差判丢帧） | EVT0 |
| \`g_pid_mid\` | i32 | PID 积分项（定点 1/1000） | EVT0 |
| \`g_cpu_load_pm\` | u16 | 主循环负载（千分比） | EVT0 |
| \`g_u8_cnt\` | u8 | 计数低 8 位（填满 ODT0 的 3 字节） | EVT0 |

DAQ 打包计划（与 \`src/xcp_test_master.c\` 注释一致）：

- list0 @EVT0(1 kHz) + 时间戳：ODT0=[ts 4B|adc 2B|u8 1B]、ODT1=[tick 4B|cpu 2B]、ODT2=[pid 4B]
- list1 @EVT1(100 Hz) 无时间戳：ODT0=[pid 4B|adc 2B]

## 实测（2026-10-30）

日志：\`logs/20261030-172706-xcp_can_daq.log\`

| 指标 | 实测 |
|---|---|
| 自证结论 | **XCP SELFTEST PASS fail=0** |
| 事件率（配置 1 kHz） | 999.0 Hz |
| list0 DAQ 帧（1 s） | 3003（3 ODT/事件） |
| DAQ 总帧率 | 3097 frame/s（500k 总线负载约 37%） |
| 事件丢失 / 队列丢弃 / TX 满 | 0 / 0 / 0 |
| 时间戳间隔 mean/min/max/sd | 1000 / 1000 / 1000 / 0 µs |
| 每事件打包开销 | 3.93 µs（空载 1.56 µs） |

复现：

~~~bash
cd hpm5e31_1
bash scripts/hwrun.sh app/xcp_can_daq --secs 25 --send "selftest 1000;stat;evstat" --lock-wait 1800
~~~

## 上位机（Bench B）

板端固件加上网关（\`gw on\`）后，主机侧：

~~~bash
bash scripts/hwrun.sh app/xcp_can_daq --secs 15 --send "gw on" \
     --post "timeout 70 bash host/xcp/run_daq.sh" --lock-wait 1800
~~~

（网关接入与 \`--post\` 的用法见 \`host/can_bridge/\` 与第 28 章；本条联调状态见第 33 章 U-34-2。）

## 易错点

见第 33 章"易错点与注意事项"：AHB SRAM 消息缓冲区、CAN 帧填充导致长度判断、
\`ALLOC_ODT_ENTRY\` 6 字节、PID 上限、时间戳吃 ODT0 容量、\`MODIFY_BITS\` 依赖 MTA。
