# mcan_two_node — CAN0 <-> CAN1 双节点真实总线测试

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

hpm5e31_1 板上 CAN0 与 CAN1 两颗 TCAN1044AV 收发器接在同一段 CAN 总线上（两端各
120 欧终端电阻），因此 MCU 内部两个 MCAN 实例即可构成一个真实的两节点 CAN 总线，
无需外部 CAN 分析仪即可验证：仲裁/应答、错误帧、位时序、ACK、总线负载。

| 命令 | 测试内容 | 关键指标 |
|---|---|---|
| `run [n]` | 经典帧 ping-pong（CAN0 发 -- CAN1 收+回显 -- CAN0 收） | 成功率、往返时间 |
| `thr [n]` | 经典帧单向流水（CAN0 连续发，CAN1 全收校验） | frame/s、payload Mbit/s |
| `fd [n]` | CAN FD ping-pong（1M 仲裁 / 2M 数据，64 B 载荷，TDC） | 成功率、LEC |
| `stat` | 读 PSR/ECR | TEC/REC、LEC、bus-off |
| `info` | 打印位时序配置 | prescaler/seg1/seg2/采样点 |

## 硬件要求 / 接线

- 板上已接好：CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 各经 TCAN1044AV 上总线，
  总线两端 120 欧（CAN0 端 + CAN1 端），**不要外加终端电阻**。
- TCAN1044AV 的 STB 由 PE03 统一控制，代码里 `board_can_transceivers_enable()` 拉低。
- 串口 /dev/ttyACM0 921600。

## 编译与运行

    bash scripts/hwrun.sh can/mcan_two_node --secs 8 --send "info;run 500;thr 2000;fd 500;stat"

## 实测现象（串口输出）

完整日志 `logs/20261030-113541-mcan_two_node.log`，关键片段：

    PINGPONG classic 500k/SP87.5: n=500 ok=500 bad_req=0 bad_echo=0 total=232772 us
      echo_latency: avg=1 us max=1 us  roundtrip_rate=2148 /s
    THROUGHPUT classic 500k/SP87.5: n=2000 rx=2000 bad=0 total=464546 us => 4305 frame/s
      payload: 0.28 Mbit/s (8B/frame @4305 fps), bus 500 kbit/s
    FD pingpong arb1M/data2M 64B: n=500 ok=500 err=0 total=316547 us => 1580 roundtrip/s lec=0

    stat: CAN0: act=1 lec=0 bo=0 ep=0 ew=0 tec=0 rec=0 tdcv=25（CAN1 同）

## 关键点 / 坑

1. **对端必须先初始化**：CAN 是带 ACK 的总线，总线上只有一个活动节点时发送端会
   一直 ACK error 并重传（TEC 快速上涨进 bus-off）。本样例先初始化 CAN1 再初始化
   CAN0，并延时 100 us 后才开始发。
2. **两节点位时序必须一致**：prescaler/seg1/seg2/采样点任一不同，高速/长总线下必然
   出现位错误。样例把两端配置封装在同一个 `node_init()` 里，避免“只改一端”。
3. **FD 数据段要开 TDC**：2 Mbit/s 以上数据段要补偿收发器环回延迟；`ssp_offset`
   按 `seg1_fd+1` 配置（SDK mcan_txrx_demo 同款），`filter_window_length` 同值。
4. **`stat` 里 TEC/REC 是诊断核心**：正常通信两者应保持 0；出现 ACK error 且 TEC
   上涨，说明对端没使能/没初始化/波特率不一致。
5. 状态灯：CAN0/CAN1 的红/白灯分别对应 TX/RX，可在 ping-pong 时观察。
