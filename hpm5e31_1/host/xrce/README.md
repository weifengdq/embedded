# host/xrce —— XRCE-DDS Agent 构建与 ROS 2 侧工具

本目录是第 35 章（XRCE-DDS 与 ROS 2 集成）的上位机部分：**构建/启动 Agent**，以及
**用 ROS 2 节点验收板端发布/订阅**的三个脚本。

## 1. 依赖

| 依赖 | 版本/位置 | 说明 |
|---|---|---|
| Micro-XRCE-DDS Agent 源码 | `ref2/Micro-XRCE-DDS-Agent`（v3.0.2，commit `a88c71`） | `build_agent.sh` 默认从这里构建 |
| ROS 2 | `/opt/ros/lyrical`（Lyrical） | 提供 Fast DDS 3.6.2、Fast CDR 2.3.6 与 `rclpy` |
| spdlog | 1.9.2（脚本自动下载并静态编译） | Agent 3.0.2 与系统 spdlog 1.15（fmt 10）不兼容 |
| 构建工具 | cmake / ninja / curl / grep | 远程机已装 |
| 网络 | 板 `192.168.2.100` ↔ 主机 `192.168.2.1`（`enp6s0`） | 主机防火墙默认关 |
| 代理 | `http://<proxy>:7890` | 仅构建时下载 spdlog 用 |

> Python 说明：`ros2` CLI 与 `rclpy` 用**系统 Python 3.14**（ROS 装在 /opt/ros/lyrical）。
> 不要用 `$HOME/pyenv`（那是 python-can 等工具的 venv，没有 rclpy）。
> 曾因系统 `python3-empy` 为 4.2.1 报 `ModuleNotFoundError: No module named 'em'`，
> 已用 `sudo pip3 install --break-system-packages empy==3.3.4` 修复（ROS 2 要求 empy 3.x）。

## 2. 构建 Agent

```bash
bash host/xrce/build_agent.sh                 # 默认构建到 ${TMPDIR}/xrce_agent_build
XRCE_BUILD_DIR=$HOME/temp/xrce bash host/xrce/build_agent.sh --prefix $HOME/temp/xrce/prefix
```

脚本做的三件事（每一步都是踩坑后的结果）：

1. 下载并 **PIC 静态编译 spdlog 1.9.2**（`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`）；
2. `cmake` Agent：`-DUAGENT_SUPERBUILD=OFF -DUAGENT_P2P_PROFILE=OFF -DUAGENT_DISCOVERY_PROFILE=OFF
   -DUAGENT_USE_SYSTEM_FASTDDS=ON -DUAGENT_USE_SYSTEM_FASTCDR=ON -DUAGENT_USE_SYSTEM_LOGGER=ON`
   （P2P/Discovery 两个 profile 要 microxrcedds_client v3.0.1，本用例不需要）；
3. `ninja install` 到 `${XRCE_BUILD_DIR}/agent_install`，最后打印 `AGENT_OK: <路径>`。

构建产物真实输出（本机实测）：

```text
[1/3] spdlog 已存在: $HOME/tmp/tp-xrce/prefix
[2/3] 配置并编译 Agent（P2P/Discovery profile 关闭：它们要 microxrcedds_client，本用例不需要）
[3/3] 安装结果
Usage: 'MicroXRCEAgent <udp4|udp6|tcp4|tpc6|canfd|serial|multiserial|pseudoterminal> <<args>>'
AGENT_OK: $HOME/tmp/tp-xrce/agent_install/bin/MicroXRCEAgent
```

## 3. 启动 Agent

```bash
bash host/xrce/run_agent.sh -d          # 后台，UDP4 :2018，日志 /tmp/xrce_agent.log
PORT=2019 VERBOSE=6 bash host/xrce/run_agent.sh      # 前台调参
```

启动成功判据：

```text
$ ss -lunp | grep 2018
UNCONN 0  0  0.0.0.0:2018 0.0.0.0:*  users:(("MicroXRCEAgent",pid=311811,fd=3))
$ tail -2 /tmp/xrce_agent.log
info | UDPv4AgentLinux.cpp | init | running... | port: 2018
info | Root.cpp | set_verbose_level | logger setup | verbose_level: 6
```

Agent 收到板端会话时日志（`-v 6`）：

```text
info | Root.cpp | create_client | create | client_key: 0x484D5035, session_id: 0x81
info | SessionManager.hpp | establish_session | session established | client_key: 0x484D5035,
       address: 192.168.2.100:58119
info | ProxyClient.cpp | create_participant | participant created | participant_id: 0x001(1)
info | DataWriter.cpp | write | [** <<DDS>> **] | client_key: 0x00000002, len: 20, data: 00 00 00 00 ...
```

## 4. 端口与 ROS 2 domain 配置

| 项 | 默认值 | 怎么改 |
|---|---|---|
| Agent 端口 | **UDP 2018** | `PORT=2019 bash host/xrce/run_agent.sh -d`；同时改板端 `cfg.agent_port` |
| 板端源端口 | 2019（xrce_pubsub）/ 2020（xrce_ros2_bridge） | 板端固件 `XRCE_LOCAL_PORT` / `LOCAL_PORT` 宏 |
| DDS domain | **0** | 主机 `export ROS_DOMAIN_ID=N`；板端固件 `cfg.domain_id = N`（XRCE Participant 的 domain_id）。**两侧必须一致，否则话题互相看不见** |
| ROS 2 话题前缀 | 板端写 `rt/<topic>` | 固件里 `topic_name` 必须带 `rt/`（`rmw_fastrtps` 的 DDS 话题名规则） |
| QoS | 板端 DataWriter RELIABLE / DataReader BEST_EFFORT | ROS 2 订阅者默认 RELIABLE，板端发布者必须 RELIABLE 才能匹配 |

## 5. ROS 2 侧工具

| 脚本 | 作用 | 关键参数 |
|---|---|---|
| `ros2_adc_sub.py` | 订阅 `/hpm5e31/adc`，统计频率/抖动/丢帧并打印 PASS/FAIL | `--topic`、`--duration`、`--reliable` |
| `ros2_cmd_pub.py` | 发布 `/hpm5e31/cmd`（UInt8）控 LED，用板端 ack 测命令往返 RTT | `--value`、`--repeat`、`--ack-topic` |
| `test_ros2_bridge.sh` | 一键跑完 1~7 步验收（话题列表/hz/echo/统计/RTT/status） | `STATUS_TOPIC`、`DUR` |

用法：

```bash
source /opt/ros/lyrical/setup.bash
ros2 topic list | grep hpm5e31
ros2 topic hz /hpm5e31/adc
ros2 topic echo /hpm5e31/adc --once
python3 host/xrce/ros2_adc_sub.py --duration 10
python3 host/xrce/ros2_cmd_pub.py --value 2 --repeat 6
bash host/xrce/test_ros2_bridge.sh           # 全部串起来
```

与 hwrun 配合（串口抓完仍持锁跑上位机测试，一次占锁完成双侧取证）：

```bash
bash scripts/hwrun.sh app/xrce_pubsub --secs 25 --send "stat" \
     --post "timeout 75 bash host/xrce/test_ros2_bridge.sh" --lock-wait 1800
```

## 6. 真实输出（2026-10-30 实测）

```text
==================== [2] ROS 2 侧可见的 XRCE 话题 ====================
/hpm5e31/adc
/hpm5e31/cmd
/hpm5e31/hello
==================== [3] ros2 topic hz /hpm5e31/adc（5 s 窗口） ====================
average rate: 100.248
	min: 0.009s max: 0.011s std dev: 0.00024s window: 40
average rate: 99.775
	min: 0.009s max: 0.011s std dev: 0.00020s window: 40
==================== [4] ros2 topic echo --once ====================
layout:
  dim: []
  data_offset: 0
data:
- 34932
- 28398
- 20428
- 4969
==================== [5] adc 订阅统计（频率/抖动/丢帧） ====================
 样本数            : 991
 测量窗口          : 9.91 s
 平均频率          : 99.89 Hz
 到达间隔抖动 σ    : 0.49 ms
 计数器丢帧        : 1（0.10 %）
 结论              : PASS
==================== [6] cmd -> LED 控制 + 命令往返时延 ====================
 发送次数          : 6
 收到 ack          : 5
 RTT 最小/中位/最大: 2.29 / 2.70 / 3.44 ms
 单向延迟上界(RTT/2): 1.72 ms
 结论              : PASS
```

## 7. 排障

| 现象 | 原因 | 处理 |
|---|---|---|
| `ros2 topic list` 无 `/hpm5e31/*` | Agent 没起 / 板端没连上 / `ROS_DOMAIN_ID` 不一致 | 先看 `ss -lunp\|grep 2018`，再看 Agent 日志有没有 `establish_session` |
| Agent 日志有 `<<DDS>> write` 但 `ros2 topic hz` 为 0 | QoS 不匹配（板端 BEST_EFFORT vs 订阅者 RELIABLE） | 板端 DataWriter 改 RELIABLE |
| `ros2` 命令报 `No module named 'em'` | 系统 empy 版本太新 | `sudo pip3 install --break-system-packages empy==3.3.4` |
| Agent 编译报 fmt 相关 static_assert | 用了系统 spdlog 1.15 | 用 `build_agent.sh` 自带的 spdlog 1.9.2 |
| `ros2 topic hz` 偶发 `failed to initialize wait set` | CLI 偶发上下文异常 | 重跑一次即可（`test_ros2_bridge.sh` 已内置一次重试） |
