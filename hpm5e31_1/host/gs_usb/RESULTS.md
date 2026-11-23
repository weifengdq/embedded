# gs_usb 实测结果（第 40 章数据源）

> 状态：**进行中（已有实测：enum / classic / 4ch / compat 部分项）**。固件（`samples/app/gs_usb_candle`，tp-infra）未就绪时，所有数字格写
> "待确认（需固件）"；固件就绪后由 tp-xrce 逐项实测填入，并在本文件底部追加日志路径。
>
> 采集环境：Ubuntu 26.04 / 内核 **7.0.0-38-generic** / can-utils(/usr/local/bin) / python-can **4.6.1** /
> iproute2 **6.19.0** / 板卡 hpm5e31_1（USB0 Pin67/68，VID:PID **1209:2323**）。
> 板内拓扑：CAN0↔CAN1、CAN2↔CAN3 物理互连 + 120Ω（约定 §5）。

## 表 A 测试矩阵结果

| # | 项目 | 命令 | 结果 | 关键数字 | 日志 |
|---|---|---|---|---|---|
| 1 | 枚举 | `bash host/gs_usb/test_enum.sh` | **PASS（10/10）** | 4 个 netdev can0..can3（ifindex 35~38），driver=gs_usb，bus-info 1-8:1.0；ethtool -T 有 hardware-transmit/receive + hardware-raw-clock | logs/20261102-103609-gs-usb-enum.log |
| 2 | 经典 500k 收发 | `bash host/gs_usb/test_classic.sh` | **PASS（11/11）** | can0->can1 与 can1->can0 双向；标准帧/29 位/DLC 边界都通过；can1 RX +9、can0 TX +17；两通道 ERROR-ACTIVE | logs/20261102-103742-gs-usb-classic.log |
| 3 | 4 通道并发 | `bash host/gs_usb/test_4ch.sh` | **PASS（12/12）** | can0/can1 与 can2/can3 两组互不串扰；TX 计数 can0=21 can1=5 can2=4 can3=1 | logs/20261102-103834-gs-usb-4ch.log |
| 4 | CAN FD（含 5M） | `bash host/gs_usb/test_5m.sh` | **PASS（R8，15/15）**：经典 500k、FD 1M/2M、**FD 1M/5M** 64 B BRS 双向各 2000/4000 帧 0 丢帧、tx_err/rx_err 0、ERROR-ACTIVE；5M 6 988.5 / 6 995.1 帧/s、64 B 载荷 3.58 Mbit/s；DLC 0~64 双向逐档一致 | 见下方 R8 节 | `logs/20261102-181452-gs-usb-5m.log` |
| 5 | 硬件时间戳 | `bash host/gs_usb/test_timestamp.sh` | **PASS（4/4 断言）** | 50/50 帧都拿到**硬件戳**（cmsg type37/len48=scm_timestamping64）；时域 **CLOCK_REALTIME**；量化 **1000 ns（1 MHz）**；单调违例 0；同帧 (软件戳-硬件戳) 中位 **67.2 µs**、σ 5.9 µs；hw/sw 间隔比值 **0.999959（-40.7 ppm）**；(收到时刻-硬件戳) 中位 81.1 µs、σ 21.7 µs | logs/20261102-105931-gs-usb-timestamp.log |
| 6 | 高负载 | `bash host/gs_usb/test_5m.sh --soak 250000` | **PASS（R8）**：1M/5M 64 B 满速 `cangen -g 0` **250 000/250 000** 帧 / 35.88 s（6 967.7 帧/s），0 丢帧、can0 TX errors/dropped 0、can1 RX errors/dropped 0；固件 tx_retry=1 872 531（背压生效）、tx_drop=0 | `logs/20261102-182018-gs-usb-5m.log` |
| 7 | 模式 | `bash host/gs_usb/test_modes.sh` | **PASS（9/9，1 SKIP）** | listen-only：能收（收到 can1 的 0x555）不能发；loopback：can0 收到自己发的 0x666；one-shot：模式位生效，但**无 ACK 时 TEC 0→0 未增长**（8 帧，记 SKIP/待固件确认） | logs/20261102-110056-gs-usb-modes.log |
| 8 | 状态与错误 | `bash host/gs_usb/test_error.sh` | **部分闭环（M4，2026-11-02 12:56）**：错误帧上报已实现——500k/250k 错配后 can0 `ip -details` 显示 **ERROR-PASSIVE**（该状态只能由 CAN_ERR 帧触发），恢复后 ERROR-ACTIVE 且收发正常；bus-off 未注入成功（TEC 只到 8/REC 127）。原始 candump 行因 SIGTERM 未 flush 未落盘，复测改用 `candump -n 5` | can0 tx 8 / rx 127（ERROR-PASSIVE），恢复后 321#AABB 收到 | logs/20261102-125008-gs-usb-berr2.log |
| 9 | 终端电阻 | `bash host/gs_usb/test_termination.sh` | **PASS（7/7）** | `ip link set can0 termination 120` 被拒绝（退出码非 0，EOPNOTSUPP 语义）；`type can ... termination 120` 同样拒绝；`ip -details` 无 termination 属性 | logs/20261102-105948-gs-usb-termination.log |
| 10 | 兼容性 | `bash host/gs_usb/test_compat.sh` | **PASS（8/8）** | python-can 4.6.1 收发 PASS；candump/cansend PASS；cangen 1000 帧 PASS；canplayer 回放收到 0x333 PASS | logs/20261102-105948-gs-usb-compat.log |

## 表 B 环境与能力（已实测，可直接引用）

| 项 | 实测值 | 采集命令 |
|---|---|---|
| 内核 | 7.0.0-38-generic | `uname -r` |
| gs_usb 驱动 | `/lib/modules/7.0.0-38-generic/kernel/drivers/net/can/usb/gs_usb.ko.zst` | `modinfo gs_usb` |
| can-utils | candump/cansend/cangen/canplayer/canfdtest `/usr/local/bin` | `command -v cangen` |
| python-can | 4.6.1 | `python -c "import can;print(can.__version__)"` |
| iproute2 | 6.19.0（支持 dbitrate/tdcv/termination） | `ip -V; ip link help can` |
| 设备出现 | **是**：`Bus 001 Device 0xx: ID 1209:2323`，Product `HPM5E31 gs_usb CANFD`，Mfr HPMicro，SN HPM5E31-GSUSB-0001 | `bash host/gs_usb/setup.sh` |
| 4 通道 netdev | **can0~can3**（ifindex 35/36/37/38），bus-info `1-8:1.0`，driver `gs_usb`（内核 7.0.0-38-generic） | `ip -br link show type can; ethtool -i can0` |

## 表 C 硬件时间戳指标（待填，字段固定）

| 指标 | 值 | 说明 / 命令 |
|---|---|---|
| `ethtool -T can0` 能力 | **hardware-transmit / hardware-receive / hardware-raw-clock**（+ software-*） | 已 advertise HW timestamp（固件 M1） |
| 硬件戳覆盖率 | 待确认 | `hw_ts / count_recv`，需 ≥90% |
| 硬件戳 vs 收到时刻偏差 | 待确认 | min/中位/max/σ（ms），来自 `test_timestamp.py` |
| 硬件戳帧间隔 | 待确认 | min/中位/max/σ（µs），发送间隔 4000 µs |
| 单调性违例 | 待确认 | 应为 0 |
| 量化步长 | 待确认 | 期望 1000 ns（1 MHz 计数器） |

## 表 D 压力与错误（待填，字段固定）

| 指标 | 值 | 说明 |
|---|---|---|
| cangen 满速帧率 | 待确认 | `cangen -g 0` 10 s，can0 TX 增量 / 时长 |
| can1 接收帧数 | 待确认 | `ip -s link` RX 增量 |
| 丢帧率 | 待确认 | (TX-RX)/TX |
| `ip -s link` dropped/errors | 待确认 | can0/can1 |
| BUS-OFF 触发耗时 | 待确认 | 位速率失配后到 `ip -details` 显示 BUS-OFF |
| BUS-OFF 恢复耗时 | 待确认 | 重启 can0 到 ERROR-ACTIVE |
| 终端电阻请求 | 待确认 | 退出码 / errno 文案（期望 EOPNOTSUPP） |
| 内核 oops/WARN | 待确认 | `dmesg --since` 扫描结果（期望 0） |

## 表 E 与其它 CAN 接入方式对比（部分待确认）

| 方式 | 延迟 | 吞吐 | CPU | 备注 |
|---|---|---|---|---|
| **gs_usb（本轮）** | 待确认（USB 侧；用 `test_timestamp.py` 偏差 + 命令往返） | 待确认（cangen 帧率） | 待确认 | 标准 SocketCAN，无需厂商驱动 |
| cannelloni（以太网隧道，第 29 章） | **2.26 ms 往返 / p99 2.49 ms**（100 次） | 300 帧/s 5 s 丢包 **0.13%** | — | 需要板端网关 + 主机 cannelloni |
| slcan（USB CDC 文本协议） | 待确认 | 待确认 | — | 未在本仓实测；文本协议开销大 |

> 表 E 的 gs_usb 行与 slcan 行在固件就绪后补齐；cannelloni 数字引自第 29 章实测，可直接对照。

## 日志汇总（每轮测试后追加）

| 时间 | 轮次 | 矩阵日志 | 备注 |
|---|---|---|---|
| 待固件 | — | — | 脚本已就绪，等 tp-infra 固件 M1 |

## 实测中踩到并修复的坑（写给第 40 章"防崩/环境"小节）

| # | 现象 | 根因 | 处理 |
|---|---|---|---|
| 1 | `ip link set canX ...` 报 `Operation not permitted`，接口一直 DOWN，后续 cansend 报 Network is down | 普通用户没有 CAP_NET_ADMIN；can-utils 也没 setcap | 全部用 `sudo -n`；`common.sh` 里自动探测 `GS_SUDO` |
| 2 | 经典帧测试 can1 一帧都收不到，但 can1->can0 正常 | 接口被上一轮 FD 测试留在 `<FD>` 模式（`ip link set canX type can bitrate ...` 不会清 FD 位），两端 FD 数据段参数不一致 | `gs_can_up` 默认显式 `fd off` |
| 3 | 测试进行到一半设备消失（`SIOCGIFINDEX: No such device`） | ①别人 hwrun 烧写复位板子；②USB autosuspend 空闲 ~85 s 把设备挂起 | 测试前用 `scripts/board_lease.sh` 独占板卡；`setup.sh` 自动关 `usbcore autosuspend=-1` + 该端口 `power/control=on` |
| 4 | CAN FD | `bash host/gs_usb/test_fd.sh` | 待确认（需实测）：tp-infra 侧 FD 2M 64B BRS 正向 100/100、反向 10/10 PASS；我方矩阵未复核（窗口内设备被别的固件占用）。验证方法：`bash host/gs_usb/test_fd.sh`（1M/2M 下 32/64B BRS + ESI + FD 下经典帧兼容） | — | — |

## 表 F 延迟 / 吞吐 / 时间戳实测（2026-11-02 11:00，自研固件 M2）

| 指标 | 实测值 | 采集脚本 |
|---|---|---|
| 单帧延迟 can0→can1（200 帧，1 ms 间隔） | min **228.2** / 中位 **267.4** / mean 265.8 / p99 **299.0** / max 693.3 µs，σ **34.1 µs**，丢 0/200 | `bench_latency.py` |
| 经典帧吞吐（cangen -g 1，5.02 s） | TX **4692** / RX **4692** 帧，**935.2 帧/s**，丢帧 **0.0%**，errors/dropped 全 0，有效载荷 411.5 kbit/s，总线负载 **20.76%** @500 kbit/s | `bench_latency.py` |
| 枚举/接口就绪（驱动 rebind ×5） | sysfs 立即出现（0.1 ms 量级，异步）；**接口可配置耗时中位 7.8 ms**（min 6.5 / max 9.0） | `bench_enum.py` |
| 硬件时间戳覆盖率 | **50/50（100%）**，cmsg=`scm_timestamping64`(48 B) | `test_timestamp.py` |
| 硬件戳时域 / 量化 | **CLOCK_REALTIME** / **1000 ns（1 MHz 计数器）** | 同上 |
| 同帧 (软件戳 − 硬件戳) | 中位 **67.2 µs**，mean 66.7，max 76.1，σ **5.9 µs** | 同上 |
| hw/sw 间隔比值 | **0.999959 → -40.7 ppm**（设备 80 MHz 时钟 vs 主机时钟的频率差；tp-infra 侧测得 -23 ppm，同量级） | 同上 |
| (收到时刻 − 硬件戳) | 中位 **81.1 µs**，σ 21.7 µs（含 USB 上传与调度） | 同上 |
| 单调性违例 | **0** | 同上 |

> 判据说明：硬件时间戳"可用"的判据是 **覆盖率 100% + 单调 + 量化 1 µs**（三项全 PASS）；
> 绝对偏差 (81 µs) 与频率差 (-40.7 ppm) 作为**精度指标**单列，不作为 PASS/FAIL 依据
> （受固件 TIMESTAMP 同步策略影响，见第 40 章"待确认项"）。

## 图（脚本生成，供第 40 章引用）

| 图号 | 文件 | 生成脚本 | 数据 |
|---|---|---|---|
| 图 40-4 | `docs/figures/40/fig-40-4-enum-capability.png` | `tools/figs/app40_perf_enum.py` | `tools/figs/app40_data.json` 的 enum 段（4 netdev + ethtool -T 能力矩阵） |
| 图 40-5 | `docs/figures/40/fig-40-5-latency-throughput.png`（**待生成**：需先跑 bench_latency.py） | `tools/figs/app40_perf_latency.py` | `tools/figs/app40_data.json` 的 latency/throughput 段 |

采集脚本：
- 枚举耗时：`sudo -n python3 host/gs_usb/bench_enum.py --json`（驱动 rebind 测 4 netdev 就绪时间）
- 延迟/吞吐：`sudo -n python3 host/gs_usb/bench_latency.py --json`（单帧 RSS 延迟 + cangen 5 s 吞吐）

## 轮次记录（含失败窗口，便于解释"为什么某项还没数字"）

| 时间 | 窗口 | 结果 | 说明 |
|---|---|---|---|
| 10:36~10:38 | 无租约 | enum PASS 10/10、classic PASS 11/11、4ch PASS 12/12、compat 7/8 | 成功轮次，日志 103609/103742/103834/103841 |
| 10:39~10:41 | 无租约（autosuspend 默认） | 中途 USB 掉线（kernel t=349936 disconnect） | 定位到 autosuspend；已固化到 setup.sh |
| 10:50~10:52 | hwrun --no-flash 持锁 | **全部假 FAIL**（"接口 can0 不存在"） | 窗口内设备被别人烧写复位两次（kernel t=350191 / t=350586 两次 USB disconnect），已删除这批日志；改用 `run_window.sh` 的"持锁前+持锁后双检" |
| 待定 | tp-infra M3 后 | timestamp / modes / termination / stress / bench | 等待窗口 |

## 一键窗口脚本

```bash
# 前置：板子上已烧好 gs_usb 固件、设备在线
bash host/gs_usb/run_window.sh 240 --no-stress
# 它会：预检设备 -> hwrun --no-build --no-flash 持锁 240s -> 二次确认 -> 依次跑
#       setup / timestamp / bench_enum / bench_latency / modes / termination / compat -> 收尾释放
```

> 失败窗口的教训：**持锁窗口开始前和拿到锁之后都要确认一次 4 个 netdev 在**（本脚本已内置，缺失即中止，不浪费窗口）。

- berr-counter tx/rx ≈ 120~128 **是速率错配下的真实错误计数**（tp-infra M3 确认：500k 对 250k 时 REC 饱和到 127、TEC 缓慢增长），**不是历史残留、也不是 GET_STATE 映射错误**；判断“有没有新增错误”要看 `ip -s link` 的 errors/dropped **增量**（本仓实测增量 0）。

## 轮次记录补充：tp-infra M3（11:20）

- 失败窗口：11:20 设备被其它方向烧写（hwrun:can_logger → someip_rpc），M3 复核窗口未果；FD/10s 压测/bus-off 三项记为**待确认（需实测）**，验证命令见上表 A。


## R5 实测补充（M4，2026-11-02 12:56）

固件新增 `bridge.c: poll_can_error_state()`（CAN_ERR_* 上报，24 B 经典帧，MODE.flags bit12 控制）。
本轮窗口受板卡排队影响，只拿到一次干净采样（/tmp/gs_final_out.txt，窗口产物），命令都在下表里可复跑。

| 项 | 命令 | 实测 | 判定 |
|---|---|---|---|
| 1M/5M 64B FD+BRS 吞吐 | `cangen can0 -f -b -g 0 -L 15 -i`（3 s） | tx/rx **29277/29277**、**9694 帧/s**（这组数本身成立）；但 **`-L 15` 实际只发 16 B**（R8 复核，见第 42 章 §42.9 坑 21），原“载荷 4.97 Mbit/s”按 64 B 算出，**数据无效** | **作废，以 R8 节为准** |
| 1M/8M（超规格） | 同上 `dbitrate 8000000` | tx/rx **41561/41561**，**13671 帧/s**，载荷 **7.0 Mbit/s**，丢帧 0 | 记录，不作可用档位（TCAN1044AV 标称 5M） |
| 1M/2M（复测） | 同上 `dbitrate 2000000` | 仅 14 帧（前一测试遗留 ERROR-PASSIVE，cangen 提前退出） | 数据作废，判定以 R4 的 2M 100/100 为准 |
| 经典 500k 稳定性 | `cangen -g 1 -n 5000` | tx/rx **4667/4667**，丢帧 0，错误 0 | PASS（短窗口） |
| 错误帧上报 | 500k can0 对 250k can1，20 帧 | can0 状态变 **ERROR-PASSIVE**（berr tx 8 / rx 127）；原始 `2000xxxx` 行未捕获 | 机制打通，原始行待复测 |
| listen-only 的 ACK | can1 `listen-only on`，can0 连发 60 帧 | can0 TEC 始终 0 → 对端仍 ACK，LISTEN_ONLY 未落到 MCAN restricted mode | **待修**，方向：app_can/bridge 的 cfg.silent 是否写进 CCCR.ASM |
| 时间戳分段漂移 | `python3 host/gs_usb/bench_ts_drift.py --secs 25 --append /tmp/ts.csv` | 普通用户 `ip link set` 被拒（EPERM），未采到 | 脚本就绪，复测需 `sudo -n` 或 `--no-config` |
| 内核 oops/WARN | 每步 `dmesg --since` 扫描 | 0 次（error / 5m 两个日志） | PASS |

## R5 轮次记录（2026-11-02 12:30~12:57）

| 窗口 | 结果 | 说明 |
|---|---|---|
| 12:33~12:44 hwrun（烧写+post） | 烧写成功、error 6 PASS/3 FAIL、5m PASS 4/4（但 cangen 被 `-p` 阻塞，数据无效） | 设备随后被别人烧写踢掉；cangen `-p` 在 TX 背压下不响应 TERM，已从脚本移除 |
| 12:54 lease + 采样 | FAIL：lease 到手时板上已不是 gs_usb 固件（0 个 can 接口） | 并行烧写窗口，非固件问题 |
| 12:55 hwrun（原子：烧写+采样） | **成功**：得到 5M/8M 吞吐、错误帧状态迁移、5 s 稳定性数据 | 见上表；/tmp/gs_final_out.txt |


## R8 实测补充（自研固件 + TX 背压修复，2026-11-02 18:14）

> 本轮先修了一个固件缺陷才测出 5M：满速 TX 时 `drain_host_tx()` 在 `app_can_send()` 失败（MCAN TX FIFO 满）时
> 丢帧且不回 echo，内核 10 个 TX URB 泄漏完就永久停 TX 队列（表现：`cansend` 报 `write: No buffer space available`、
> `tx_packets` 不再增长，但设备在线、berr 0/0、无 oops）。修法：保留队尾下轮重试（背压）+ echo 暂存补发 + `tx_retry` 计数。
> 第 42 章 §42.7.8 与 §42.9 坑 20 有完整描述。

| 项 | 命令 | 实测 | 判定 | 日志 |
|---|---|---|---|---|
| 经典 500k 双向突发 | `test_5m.sh --core`（cangen -g 0，2000 帧/方向） | 4 375.3 / 4 376.9 帧/s，**2000/2000**，丢帧 0、错误 0 | PASS | `logs/20261102-181452-gs-usb-5m.log` |
| FD 1M/2M 64 B 双向 | 同上 | 3 192.3 / 3 194.3 帧/s，**2000/2000**，丢帧 0、错误 0 | PASS | 同上 |
| **FD 1M/5M 64 B 双向** | 同上（4000 帧/方向） | **6 988.5 / 6 995.1 帧/s**，**4000/4000**，丢帧 0、tx_err/rx_err 0、ERROR-ACTIVE，64 B 载荷 **3.58 Mbit/s** | **PASS** | 同上 |
| FD DLC 0~64 双向 | `test_5m.sh --ext` | 16/16 档长度一致（0~8/12/16/20/24/32/48/64） | PASS | 同上 |
| 5M 分段突发 | 6 轮 x 1000 帧交替方向 | 每轮 tx=rx=1000，累计 6000 帧，berr 0/0 | PASS | 同上 |
| **5M 满速长测** | `test_5m.sh --soak 250000` | **250 000/250 000 帧 / 35.88 s（6 967.7 帧/s）**，丢帧 0、错误 0；固件 `tx_retry=1 872 531`、`tx_drop=0`、`in_drop=0` | **PASS** | `logs/20261102-182018-gs-usb-5m.log` |
| 1M/8M 64 B（超规格） | `test_5m.sh --ext` | 9 981.4 帧/s、5.11 Mbit/s、1000/1000 帧 0 丢帧 | 记录，不作档位（TCAN1044AV 标称 5M） | `logs/20261102-181452-gs-usb-5m.log` |
| **A/B：修复前** | `repro_tx_urb_leak.sh --expect-stall` | 1M/2M 64 B 满速只发出 **5/3000 帧**，`tx_packets` 8→8，`cansend` 报 `No buffer space available` | 复现成功 | `logs/20261102-182213-gs-usb-tx-urb-leak.log` |
| **A/B：修复后** | `repro_tx_urb_leak.sh --expect-ok` | **3000/3000 帧**，`cansend` rc=0，`tx_packets` 3003→3004 | 通过 | `logs/20261102-182303-gs-usb-tx-urb-leak.log` |
| dmesg oops/WARN | 每步 `dmesg --since <基线>` | 全程 0 命中 | PASS | 各日志内 |

**R8 结论**：自研 gs_usb 固件在 1 Mbit/s 仲裁 + **5 Mbit/s 数据段**、64 B FD+BRS 下双向 0 丢帧、0 错误；
25 万帧满速长测无卡死、无内核异常。5M 的原“无归档数字”（R5 的 4.97 Mbit/s）已作废——那批数据用 `-L 15`
实际只发了 16 B（R8 坑 21）。>1 h 长稳仍是已知限制（本轮最长 35.88 s 连续满速）。
