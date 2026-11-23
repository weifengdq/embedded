# samples/app/xrce_pubsub —— Micro-XRCE-DDS 最小闭环（2 发布 + 1 订阅）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

板端通过 **XRCE-DDS**（lwIP UDP custom transport）把数据发布到主机上的 Agent，再由 Agent 转成
ROS 2 话题；同时订阅一个 ROS 2 话题控制板载 LED。

## 1. 硬件连接（Bench C）

| 资源 | 连接 |
|---|---|
| 千兆以太网 | 板 ENET0 RGMII + YT8011AN(1000BASE-T1) ↔ 千兆车载以太网盒 ↔ 主机 `enp6s0` |
| 地址 | 板 `192.168.2.100/24`，主机 `192.168.2.1/24`（GW 192.168.2.1） |
| Agent | 主机 `MicroXRCEAgent udp4 -p 2018`，板 → `192.168.2.1:2018`（源端口 2019） |
| ADC | PF26=ADC0.IN1、PF28=ADC0.IN5、PF30=ADC0.IN6（J3 排针；悬空读数即噪声底） |
| LED | USR LED PA02（订阅 `/hpm5e31/cmd` 控制） |

## 2. XRCE 实体

| 方向 | DDS 话题（板端写全名） | 类型 | 周期 | 流 / QoS |
|---|---|---|---|---|
| 发布 | `rt/hpm5e31/hello` | `std_msgs::msg::dds_::String_` | 10 Hz | 可靠流 `0x80`，RELIABLE |
| 发布 | `rt/hpm5e31/adc` | `std_msgs::msg::dds_::UInt16MultiArray_` | 100 Hz | BE 流 `0x01`，DDS QoS RELIABLE |
| 订阅 | `rt/hpm5e31/cmd` | `std_msgs::msg::dds_::UInt8_` | 事件 | 可靠输入流 `0x80`，BEST_EFFORT |

`/hpm5e31/adc` 的 4 个 u16：`[ADC0.IN1, ADC0.IN5, ADC0.IN6, 采样序号 & 0xFFFF]`
（第 4 路放序号是为了在主机侧直接算丢帧率；真正第 4 路 ADC 的引出焊盘待确认，见第 35 章 U-36-2）。

## 3. 编译与上板

```bash
cd hpm5e31_1
# 主机：构建 + 启动 Agent（一次即可）
bash host/xrce/build_agent.sh
bash host/xrce/run_agent.sh -d

# 板端：编译 + 烧写 + 抓 25 s 串口；串口抓完仍持锁跑 ROS 2 验证（一次占锁完成双侧取证）
bash scripts/hwrun.sh app/xrce_pubsub --secs 25 --send "stat" \
     --post "timeout 75 bash host/xrce/test_ros2_bridge.sh" --lock-wait 1800
```

板端 shell：`xrce`（会话/实体/统计）、`stat`（计数器 + PASS/FAIL）、`pub <text>`（手动发一条）、`rst`。

## 4. 实测（2026-10-30，日志 `logs/20261030-172701-xrce_pubsub.log`）

板端串口：

```text
[xrce] init rc=0 state=connected
[xrce] entities: hello=0 adc=1 cmd=0
[xrce] state=connected up=22074 ms hello=200 adc=2000 cmd=0 led=0
[xrce] tx: samples=2200 dg=2214 bytes=70126 drop=0 err=2 | rx: dg=215 bytes=2810 samples=0 ovr=0
[xrce] reliable: acked=204 retrans=0 reconnects=0 pub_err=0
[xrce] STAT hello=9.1/s adc=90.6/s acked=204 => PASS
```

ROS 2 侧（`--post` 期间采集）：

| 指标 | 实测 |
|---|---|
| `ros2 topic hz /hpm5e31/adc` | **100.25 / 99.78 / 99.85 Hz**（3 个窗口，min 9 ms / max 11 ms） |
| `ros2_adc_sub.py --duration 10` | 991 样本 / 9.91 s = **99.89 Hz**，抖动 σ=0.49 ms，丢帧 **0.10%**（1/991） |
| `ros2_cmd_pub.py --value 2 --repeat 6` | 6 发 5 ack，RTT **2.29 / 2.70 / 3.44 ms**（min/中位/max），单向上界 1.72 ms |
| `ros2 topic echo /hpm5e31/adc --once` | `layout.dim=[]`, `data=[34932,28398,20428,4969]` |
| LED 控制 | 串口 `[cmd] rx UInt8=2 -> LED ON/OFF` 与 ROS 2 命令一一对应 |

> 板端自报 `adc=90.6/s` 是因为分母含约 2 s 的建会话时间，**权威频率以 ROS 2 侧 100 Hz 为准**。

## 5. 坑

1. **不要用 `sleep` 作为 shell 命令**：letter-shell 没有 `sleep`；`hwrun --send` 只是按分号拆行发送，
   需要延时请拆成两次 hwrun（第二次 `--no-build`）或自己在固件里注册命令。
2. **`--send` 必须加引号**，否则外层 shell 会把分号后的命令吃掉。
3. Agent 必须提前启动，否则板端 `init rc=-3`（建会话失败）；板端有 5 s 无 ACK → 2 s 重连逻辑。
4. 串口打印 5 s 一次，`--secs` 至少 15 才能看到 PASS 行。
5. 板端 `err=2` 是启动瞬间 ARP 未解析的两次 `udp_send` 失败，属正常现象（之后归零）。

## 6. 文件

| 路径 | 说明 |
|---|---|
| `CMakeLists.txt` | lwIP + YT8011 底座 + `app_xrce` 组件 |
| `inc/lwipopts_app.h`、`inc/common_cfg.h` | 沿用 SDK `lwip_udpecho` 的内存预算 |
| `src/main.c` | 实体创建、CDR 编码、ADC 采样、LED、shell 命令、统计 |
| `src/phy_glue.c` | 把 YT8011AN 驱动接到 SDK lwIP 移植层（与 `enet_lwip_iperf` 同一做法） |
