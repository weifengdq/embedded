# host/ —— 上位机工具总览（第二阶段：应用层协议）

本目录放**下位机协议对应的上位机工具**：每个方向一个子目录，内含源码 + README（依赖/构建/运行/真实输出）+ 一键脚本。
板上固件在 `samples/app/<name>`，书稿章节在 `docs/chapters/29~39`。

## 1. 语言矩阵

用户要求上位机覆盖 **C++ / Python / Rust** 至少各一种。当前分布：

| 方向 | 协议 / 用途 | Python | C++ | Rust | 入口（推荐先看的文件） |
|---|---|---|---|---|---|
| `can_bridge/` | cannelloni 兼容 CAN 网关（vcan0 ↔ 板 CAN1） | ✅ | —（外部 cannelloni 为 C） | — | `acceptance.sh`、`bench_gateway.py`、`udp2vcan.py` |
| `canlog/` | candump 记录解析与回放校验 | ✅ | — | ✅（`candump2csv` 可替代/互补） | `candump_parse.py` |
| `canopen/` | CANopen 节点验收（PDO/SDO/心跳） | ✅ | — | — | `co_node_test.py`、`co_pdo_bench.py` |
| `j1939/` | J1939 地址声明/DM1/TP/监控 | ✅ | — | — | `j1939_monitor.py`、`dm1_decode.py` |
| `ptp/` | gPTP 主/从与同步精度统计 | ✅ | — | — | `ptp_stats.py`、`run_ptp_master.sh` |
| `someip/` | SOME/IP 客户端（SD/方法/事件/TP） | ✅ | ✅（vsomeip_client） | — | `someip_client.py`、`vsomeip_client/` |
| `uds/` | UDS 诊断（ISO 14229，经 ISO-TP） | ✅ | — | ✅（`udsdec` 解码命名） | `uds_client.py` 等 |
| `vss/` | 车辆信号规范（VSS）映射 | ✅ | — | — | `vss_*.py` |
| `xcp/` | XCP 标定/DAQ | ✅ | — | — | `xcp_cal.py`、`xcp_daq.py` |
| `xrce/` | Micro-XRCE-DDS Agent + ROS 2 验收 | ✅ | — | — | `run_agent.sh`、`ros2_adc_sub.py`、`test_ros2_bridge.sh` |
| `rust/` | 离线解析类工具（candump / ISO-TP / UDS） | — | — | ✅ | `candump2csv`、`isotp`、`udsdec` |

汇总：**Python 覆盖 11 个方向**（主力），**C++ 1 个方向**（SOME/IP 的 vsomeip 客户端），
**Rust 3 个 crate**（candump2csv / isotp / udsdec，覆盖 CAN 日志与 UDS/ISO-TP 解析）。
需要“同一个方向用两种语言做对照”的，看 `canlog/`（Python）与 `rust/candump2csv`（Rust）、
`uds/`（Python 诊断流程）与 `rust/udsdec`（Rust 报文解码）。

## 2. 运行环境

| 用途 | 解释器 / 工具链 | 说明 |
|---|---|---|
| 大部分 Python 工具 | `$HOME/pyenv/bin/python` | 已装 python-can / can-isotp / udsoncan / canopen / pyxcp / cantools / numpy / matplotlib |
| **ROS 2 工具**（`host/xrce/*.py`） | **系统 python3（3.14）** | `source /opt/ros/lyrical/setup.bash` 后用系统 python3；pyenv 里没有 rclpy |
| Rust 工具 | `cargo` / `rustc` 1.97.1 | 纯 std，**无外部 crates**，离线可 build/test |
| C++ 工具 | CMake + g++（vsomeip 库） | 见 `host/someip/README.md` |

CAN 相关工具（vcan0 / cansend / candump / canplayer）来自 `can-utils`：
`sudo apt install can-utils`。

## 3. 通用约定

* 每个子目录一个 `README.md`：依赖 → 构建 → 运行 → **真实输出片段** → 排障；
* 上位机脚本要能在主机独立跑（除板卡/Agent 外无其它外部依赖），失败时打印 `FAIL` 并返回非 0；
* 需要板卡的测试统一走 `bash scripts/hwrun.sh <sample> --post "timeout N bash host/.../xxx.sh" --lock-wait 1800`，
  这样串口抓取与上位机测试在同一把板锁内完成；
* 产物目录（`build/`、`target/`）不入 git，见各目录的 `.gitignore`。

## 4. 快速索引

| 想做的事 | 命令 |
|---|---|
| 跑一遍 CAN 网关验收（Bench B） | `bash scripts/hwrun.sh app/can_gateway --secs 15 --send "gw on;quiet" --post "timeout 70 bash host/can_bridge/acceptance.sh" --lock-wait 1800` |
| 解析板端导出的 candump | `host/rust/target/release/candump2csv /tmp/candump.txt --csv /tmp/frames.csv` |
| 解析 UDS 报文名称 | `host/rust/target/release/udsdec 7F2231` |
| 手工组 ISO-TP 帧 | `host/rust/target/release/isotp seg --hex 22F190 --pad CC` |
| 起 XRCE Agent 并验收 ROS 2 桥接 | `bash host/xrce/run_agent.sh -d` 然后 `bash host/xrce/test_ros2_bridge.sh` |
| SOME/IP 客户端 | `bash host/someip/run_client.sh` |
| J1939 监控 | `$HOME/pyenv/bin/python host/j1939/j1939_monitor.py --help` |
