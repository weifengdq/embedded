# samples/app/xrce_ros2_bridge —— 面向 ROS 2 的 XRCE-DDS 桥接验证固件

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

与 `xrce_pubsub` 使用**同一个** `components/app_xrce` 组件，但只保留 ROS 2 桥接必需的三个实体，
并额外发布一个 **status 字符串**（1 Hz），把板端计数器直接送到 ROS 2 侧，便于"板端发了多少 / ROS 2 收到多少"对账。

## 1. 与 xrce_pubsub 的差异

| 项 | xrce_pubsub | xrce_ros2_bridge |
|---|---|---|
| 实体 | hello(10 Hz) + adc(100 Hz) + cmd 订阅 | adc(100 Hz) + status(1 Hz) + cmd 订阅 |
| 本地端口 | 2019 | **2020** |
| client_key | `0x484D5035` | `0x42524944` |
| 状态输出 | `stat` 打印计数器 | `status` 话题（ROS 2 侧 `ros2 topic echo` 可读）+ `brstat` |
| 适用 | 通用发布订阅演示 | ROS 2 桥接验收、时序/计数对账 |

## 2. 硬件与网络

同 `xrce_pubsub`：板 `192.168.2.100/24` ↔ 主机 `192.168.2.1/24`，Agent `udp4 -p 2018`；
ADC 用 PF26/PF28/PF30，LED 用 PA02。

## 3. 运行

```bash
cd hpm5e31_1
bash host/xrce/run_agent.sh -d                    # 主机：Agent（只需一次构建）

# 板端 + ROS 2 双侧验证（一次占锁；STATUS_TOPIC 指向本固件的 status 话题）
STATUS_TOPIC=/hpm5e31/status DUR=10 \
bash scripts/hwrun.sh app/xrce_ros2_bridge --secs 20 --send "brstat" \
     --post "timeout 75 bash host/xrce/test_ros2_bridge.sh" --lock-wait 1800
```

板端 shell：`brstat`（打印计数与 PASS/FAIL）、`brrst`（清零）。

## 4. ROS 2 侧验收脚本做什么

`host/xrce/test_ros2_bridge.sh` 依次执行：

1. 检查 Agent 是否监听 2018；
2. `ros2 topic list | grep hpm5e31` —— 确认 `/adc`、`/cmd`、`/status` 可见；
3. `ros2 topic hz /hpm5e31/adc` —— 频率与到达间隔；
4. `ros2 topic echo /hpm5e31/adc --once` —— 内容与 CDR 布局；
5. `ros2_adc_sub.py` —— 频率/抖动/丢帧统计（PASS/FAIL）；
6. `ros2_cmd_pub.py` —— 发 `/cmd` 控 LED，并用 status 里的 `cmd=... ack=...` 测往返 RTT；
7. `ros2 topic echo $STATUS_TOPIC --once` —— 板端状态串。

## 5. 实测

2026-10-30 17:43 上板实测（串口 `logs/20261030-174309-xrce_ros2_bridge.log`）：

板端串口（`up=12015 ms` 那一行）：

```text
[bridge] up=12015 ms state=connected adc=1000 (100.5 Hz) cmd=0 ack_tx=14 status=10
[bridge] tx dg=1023 bytes=32977 drop=0 err=2 | rx dg=24 samples=0 retrans=0
[bridge] STAT adc_hz=100.5 cmd=0 => PASS
```

ROS 2 侧（`--post test_ros2_bridge.sh`）：

| 指标 | 实测 |
|---|---|
| `ros2 topic hz /hpm5e31/adc` | **100.07 / 99.93 / 100.06 Hz** |
| `ros2_adc_sub.py --duration 10` | 994 样本 = **100.01 Hz**，抖动 σ=0.29 ms，丢帧 **0.00%** |
| `ros2_cmd_pub.py --ack-topic /hpm5e31/status` | 6 发 6 ack，RTT **2.51 / 3.40 / 3.48 ms**，单向上界 1.74 ms |
| `ros2 topic echo /hpm5e31/status --once` | `adc=6300 cmd=6 ack=63 up=65067`（与 6 次命令一致） |
| 话题列表 | `/hpm5e31/adc`、`/hpm5e31/cmd`、`/hpm5e31/status` |
| 结论 | `BRIDGE_DONE rc=0`（全部 PASS） |

## 6. 文件

| 路径 | 说明 |
|---|---|
| `CMakeLists.txt` | 与 pubsub 相同的 lwIP/YT8011 底座 + `app_xrce` |
| `src/main.c` | adc/status/cmd 三实体 + CDR 编码 + 计数打印 |
| `src/phy_glue.c`、`inc/*` | 同上 |
| `host/xrce/test_ros2_bridge.sh` | ROS 2 侧验收脚本（被 `--post` 调用） |
