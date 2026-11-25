# host/gs_usb —— gs_usb（candleLight）适配器上位机测试矩阵

把 **hpm5e31_1 做成标准 gs_usb / candleLight 4 路 CAN FD USB 适配器** 之后，用**标准 SocketCAN 工具**
（不碰任何厂商私有协议）验证它到底合不合格。本目录是第 39 章（gs_usb/candleLight）的实测证据来源。

- 固件：`samples/app/gs_usb_candle`（tp-infra 负责，VID:PID = `1209:2323`）
- 协议契约：`docs/gs_usb开发约定.md`（§3 协议事实表、§7 防崩规则、§8 测试矩阵）
- 实测结果：`RESULTS.md`（本目录，随测试更新）

## 1. 前置条件

| 项 | 要求 | 检查命令 |
|---|---|---|
| 内核 | **7.0.0-38-generic**（契约指定；gs_usb.ko 已内置） | `uname -r` |
| 驱动 | `gs_usb.ko` 存在（插上设备后 udev 自动加载） | `modinfo gs_usb | head -3` |
| 固件 | 板子 USB 口（Pin67/68）插到主机，`lsusb` 出现 `1209:2323` | `lsusb | grep 1209` |
| can-utils | candump / cansend / cangen / canplayer / canfdtest | `command -v cangen` |
| python-can | 4.x（pyenv 里的 `$HOME/pyenv/bin/python`） | `python -c "import can;print(can.__version__)"` |
| iproute2 | 支持 `dbitrate` / `termination` 关键字（≥ 6.x） | `ip -V; ip link help can` |
| sudo | **免密 sudo（必需）**：CAN 配置要 CAP_NET_ADMIN，CAN_RAW socket / candump / cansend / python-can 要 CAP_NET_RAW；读 dmesg 也走 sudo（kernel.dmesg_restrict=1） | `sudo -n ip link set can0 down`；`sudo -n dmesg | tail -1` |

**需要免密 sudo**（本机 z 用户没有 cap_net_admin/cap_net_raw，can-utils 也没 setcap）——脚本用 `GS_SUDO="sudo -n"` 前缀自动处理，并在 `setup.sh` 里做权限探测；**不需要改 udev 规则**（内核 gs_usb 驱动自带 `1209:2323` id_table）。
唯一需要 root 的是读内核日志；脚本用 `sudo -n dmesg` 兜底，若 sudo 不可用会直接判"安全检查无法执行"并拒绝继续。

## 2. 文件清单

| 文件 | 作用 | 典型耗时 |
|---|---|---|
| `common.sh` | 公共库：日志落盘、dmesg 基线 + oops/WARN 检测、接口发现、PASS/FAIL 汇总 | — |
| `setup.sh` | 环境预检 + 设备枚举 + 等 can0..3 出现 + 每个接口的 `ethtool -i/-T` 快照 | 15 s |
| `teardown.sh` | 关接口、保存 dmesg 尾部、`--unload` 可卸载模块 | 5 s |
| `run_all.sh` | 矩阵总入口（串行 + 内核异常即中止 + 汇总成 Markdown 表） | 3~4 min |
| `test_enum.sh` | **项 1** 枚举：USB 描述符、dmesg、4 个 netdev、驱动绑定、时间戳能力 | 10 s |
| `test_classic.sh` | **项 2** 经典 500k：标准帧/29 位/0 字节/8 字节/RTR，双向，ip -s link 计数 | 20 s |
| `test_fd.sh` | **项 4** CAN FD：1M 仲裁 + 2M/5M/8M 数据、BRS、ESI、32/64 字节，FD 下经典帧兼容 | 40 s |
| `test_5m.sh` | **项 11** CAN FD 5 Mbit/s 双向满速矩阵：经典 500k / FD 1M-2M / FD 1M-5M、DLC 0~64、6 轮突发、autosuspend；`--soak N` 满速长测（R8） | 25 s（矩阵）/ 40 s（长测） |
| `repro_tx_urb_leak.sh` | **项 12** TX URB 泄漏 A/B 复现：`--expect-stall`（旧固件）/ `--expect-ok`（修复后），判据是 `cansend` 是否报 ENOBUFS（R8 易错点 20） | 15 s |
| `test_4ch.sh` | **项 3** 4 通道并发（can0→can1、can2→can3，两组互不串扰） | 20 s |
| `test_timestamp.py` | **项 5** 原始 socket + `SO_TIMESTAMPING(RAW_HARDWARE)` 解析 `scm_timestamping64`，统计硬件戳覆盖率/偏差/抖动/量化步长 | 15 s |
| `test_timestamp.sh` | 上一项的 shell 包装（统一 GS_RESULT 与日志格式） | 15 s |
| `test_stress.sh` | **项 6** `cangen -g 0` 高压 10 s + `ip -s link` 丢包/错误计数 | 25 s |
| `test_modes.sh` | **项 7** listen-only / loopback / one-shot 行为验证 | 30 s |
| `test_error.sh` | **项 8** 位速率失配造 bus-off → GET_STATE/ip -details → 恢复 | 30 s |
| `test_termination.sh` | **项 9** `ip link set can0 termination 120` → 期望 EOPNOTSUPP | 10 s |
| `test_compat.sh` | **项 10** python-can + candump/cansend/cangen/canplayer 兼容性 | 30 s |
| `run_window.sh` | **一键窗口**：预检设备→持锁(不烧写)→跑 timestamp/bench/modes/termination/compat(/stress)→释放 | 3.5~4 min |
| `bench_enum.py` | 枚举/接口就绪耗时（驱动 rebind 测 4 netdev 就绪时间），供图 41-4 | 15 s |
| `bench_latency.py` | 单帧延迟（can0→can1）+ 经典帧吞吐（cangen 5 s），供图 41-5 | 15 s |
| `RESULTS.md` | 实测数字表（供第 39 章直接引用） | — |

## 2.5 独占板卡（**必做**，否则会假 FAIL）

gs_usb 测试要求"设备在整个测试期间持续在线"。任何人的 `hwrun.sh` 烧写都会复位板子 → USB 设备消失 →
测试看起来像"固件崩溃/掉线"。所以**测试前先用板卡租约把板子占住**（租约期间别人 hwrun 会排队）：

```bash
cd hpm5e31_1

# 1) 后台拿 300 s 板卡租约（名字随便写，便于 cat 锁文件看谁占着）
nohup bash scripts/board_lease.sh 300 gs_usb >/tmp/gs_usb_lease.log 2>&1 &
cat /tmp/hpm5e31_hw.lock.d/what          # 确认 what=gs_usb

# 2) 跑矩阵（设备必须已经在跑 gs_usb 固件，租约本身不烧写）
bash host/gs_usb/quick_window.sh          # 90 s 窗口：enum + classic
bash host/gs_usb/run_all.sh --safe        # 2~3 min：enum/classic/4ch/stress/compat
bash host/gs_usb/test_timestamp.sh        # 硬件时间戳

# 3) 释放租约
kill %1                                   # 或 pkill -f board_lease.sh
```

> 若设备当前不在（`/sys/class/net/can0` 不存在），说明板子上跑的不是 gs_usb 固件，
> 需要先让固件负责人（tp-infra）烧好并保持运行，租约不能替代烧写。

## 3. 快速开始

```bash
cd hpm5e31_1

# 0) 让板子跑 gs_usb 固件（由 tp-infra 烧写；烧完不要断电、不要再烧别的固件）
bash scripts/hwrun.sh app/gs_usb_candle --secs 10 --send "stat" --lock-wait 1800
#    （固件运行后 USB 上出现 1209:2323，主机自动生成 can0..can3）

# 1) 预检 + 枚举
bash host/gs_usb/setup.sh

# 2) 全矩阵（串行；任一步内核 oops/WARN 会立即中止）
bash host/gs_usb/run_all.sh

# 3) 只跑某几项 / 快速子集
bash host/gs_usb/run_all.sh --only "classic termination"
bash host/gs_usb/run_all.sh --fast          # enum/classic/fd/termination
bash host/gs_usb/run_all.sh --safe          # 安全子集：enum/classic/4ch/stress/compat（不碰 FD/错误注入）

# 4) 单项
bash host/gs_usb/test_classic.sh
python3 host/gs_usb/test_timestamp.py --count 200 --gap 0.002

# 5) 收尾
bash host/gs_usb/teardown.sh
```

### 3.1 推荐：用 `run_window.sh` 一次跑完（避免被烧写打断）

```bash
cd hpm5e31_1
# 前置：tp-infra 已烧好 gs_usb 固件且设备在线（lsusb 有 1209:2323、ip -br link 有 can0~can3）
bash host/gs_usb/run_window.sh 240 --no-stress
```

它会：①预检设备/4 接口 → ②后台 `hwrun --no-build --no-flash` 持锁 240 s（不烧写） → ③拿锁后二次确认 → ④依次跑 setup/timestamp/bench_enum/bench_latency/modes/termination/compat → ⑤接口 down、释放锁、把每项一行结果写到 `logs/<ts>-gs-usb-window2.log`。

> 若"持锁后设备不在"（被别人的烧写打断），脚本会**立即中止**而不是空跑 4 分钟。

> **板卡独占**：gs_usb 固件运行期间 USB 设备就是板子本身，**不能烧别的固件**（会掉设备）。
> 测试前在群里同步；上板/烧写仍走 `scripts/hwrun.sh`。

## 3.1 基线固件现状与测试策略（2026-11-02）

拿到设备后的第一轮是 **参考基线固件**（`Roboparty/roboparty_EtherCANFD` 原样），它有已知缺陷，
所以测试策略分两档，**不要用基线固件出正式数据**：

| 现象（基线固件） | 影响 | 对策 |
|---|---|---|
| 枚举后约 9 s USB 自己掉线 | 长测试会中途丢设备 | 只用 **≤90 s 短窗口**，且每个测试结束都做存活检查（`gs_assert_alive`） |
| **FD 首帧后 USB 断开/设备崩溃** | test_fd 会让设备掉线 | **基线阶段不跑 test_fd**；等自研固件 M1 |
| 不 advertise 硬件时间戳（`ethtool -T` 只有软件戳） | test_timestamp 必然 FAIL | 基线阶段标"基线不支持，待自研固件"，不记 FAIL |
| CAN2 RX 引脚与本板不同 | 4 通道并发里 can2→can3 可能不通 | test_4ch 里对 can2/can3 记录实际现象，不臆造 |

**阶段 1（基线固件，短窗口）**：`--safe` + `--only "enum classic"`，只验证"能被标准 SocketCAN 认出来、经典帧能通"。
**阶段 2（自研固件 M1 之后，正式数据）**：跑完整 10 项；`run_all.sh` 一次跑完并汇总成 Markdown 表。

> 每个测试脚本结束时会：
> 1) 用 sysfs 扫 `/sys/bus/usb/devices/*/` 找 `idVendor=1209 / idProduct=2323`（**不用 lsusb**——本机 AppArmor
>    会拦 lsusb 读 `uevent`，导致误报"无设备"）；
> 2) 若设备已消失，记 FAIL 并把 `dmesg` 尾部存到 `/tmp/gs_usb_artifacts/dmesg-alive-*.log`；
> 3) 再做内核 oops/WARN 扫描。

## 4. 测试矩阵与判定标准

| # | 项目 | 命令 | PASS 判定 | 当前状态 |
|---|---|---|---|---|
| 1 | 枚举 | `test_enum.sh` | `lsusb` 有 1209:2323；dmesg 有 gs_usb 枚举；`can0..can3` 全在且 `driver=gs_usb`；`ethtool -T` 有硬件时间戳能力 | 待确认（需固件） |
| 2 | 经典 500k | `test_classic.sh` | can1 收到 `123 [8] …`、`321 [3] …`、`18DAF110 [4] …`、`100 [0]`；反向亦可；ip -s link 计数 ≥4 | 待确认（需固件） |
| 3 | 4 通道并发 | `test_4ch.sh` | 4 通道各自可发；can1 只见 0x100、can3 只见 0x200，两组无串扰 | 待确认（需固件） |
| 4 | CAN FD | `test_fd.sh` | 1M/2M 下 32/64 字节 BRS 帧完整收到；5M/8M 若不支持记 SKIP 并保留错误 | 待确认（需固件） |
| 5 | 硬件时间戳 | `test_timestamp.sh` / `test_timestamp.py` | 硬件戳覆盖率 ≥90%；单调；与系统时钟偏差 σ<2 ms；量化步长 1000 ns（1 MHz） | 待确认（需固件） |
| 6 | 高负载 | `test_stress.sh` | cangen 满速 10 s 的帧率/丢帧率给出数字；无 `ip -s link` 异常丢包；无 dmesg WARN | 待确认（需固件） |
| 7 | 模式 | `test_modes.sh` | listen-only 能收不能发；loopback 自发自收；one-shot 8 帧后仍 `ERROR-WARNING/ACTIVE`（不 BUS-OFF） | 待确认（需固件） |
| 8 | 状态/错误 | `test_error.sh` | 失配后 `ip -details` 显示 `BUS-OFF`；恢复后回 `ERROR-ACTIVE` 且收发正常 | 待确认（需固件） |
| 9 | 终端电阻 | `test_termination.sh` | `termination 120` 退出码非 0 且为 EOPNOTSUPP；`ip -details` 无 `termination` 属性 | 待确认（需固件） |
| 10 | 兼容性 | `test_compat.sh` | python-can 收发；candump/cansend/cangen/canplayer 全部正常 | 待确认（需固件） |
| 11 | **CAN FD 5 Mbit/s** | `test_5m.sh` | 1M/5M 64 B BRS：can0→can1 与 can1→can0 各 **4000/4000** 帧、0 丢帧、tx_err/rx_err 0、ERROR-ACTIVE；25 万帧满速长测无卡死；dmesg 0 命中 | **PASS（R8，2026-11-02）** |

> 表格数字/结论在固件就绪后填 `RESULTS.md` 与本表"当前状态"列，并把 `logs/` 路径写进第 39 章。

## 5. 防内核崩溃（安全机制实现）

对应开发约定 §7，脚本层面强制：

1. **dmesg 基线**：每个测试开始时记录 `date +%s`，之后用 `dmesg --since "@<epoch>"` 只扫描新增行
   （不用行号——dmesg 环形缓冲会淘汰旧行，行号会倒退）；
2. **命中即停**：新增行命中 `Oops|BUG:|WARNING:|kernel panic|Call Trace|RCU stall|general protection|KASAN` 时，
   打印 `[STOP]`、把**完整 dmesg** 存到 `/tmp/gs_usb_artifacts/dmesg-full-<test>-<ts>.log`、
   以退出码 **9** 结束；`run_all.sh` 见到 9 立即中止后续所有测试；
3. **只发合法帧**：channel 只取枚举到的 0..3；经典帧 DLC 0..8、FD 帧 0..64（`cansend` 用十六进制数据长度，
   不给非法 DLC）；不发越界 ID（11 位 ≤0x7FF、29 位加 `8 位 ID` 前缀）；
4. **只用标准 SocketCAN**：`ip link` / `cansend` / `candump` / `cangen` / `canplayer` / python-can / 原始 `CAN_RAW` socket，
   **不碰 usbfs / libusb**，避免绕过驱动的未定义行为；
5. **破坏性最小的错误注入**：bus-off 用"两端位速率失配"（可恢复、标准手段），不拔线、不改硬件；
6. **测试顺序**：`单帧 → FD → 时间戳 → 4 通道 → 压力 → 模式 → 错误 → 终端电阻 → 兼容`（与约定 §7.6 一致）。

## 6. 本机环境预检（真实输出，2026-11-02）

```text
$ uname -r
7.0.0-38-generic

$ modinfo gs_usb | head -2
filename:       /lib/modules/7.0.0-38-generic/kernel/drivers/net/can/usb/gs_usb.ko.zst
description:    Socket CAN device driver for Geschwister Schneider ... candleLight USB CAN interfaces.

$ ip -V
ip utility, iproute2-6.19.0, libbpf 1.6.3

$ ip link help can | grep -E 'termination|dbitrate'
	[ dbitrate BITRATE [ dsample-point SAMPLE-POINT] ] |
	[ termination { 0..65535 } ]

$ python -c "import can;print(can.__version__)"
4.6.1

$ lsusb | grep 1209 || echo "no candleLight device"
no candleLight device        # ← 固件未就绪（2026-11-02 10:1x）

$ ls -d /sys/class/net/can* 2>/dev/null || echo "no can ifaces"
no can ifaces
```

> 上面"无设备"是**当时**的事实；固件就绪后本节的 `lsusb` / `ip -br link` 部分会自动变成 can0..can3。

## 7. 日志与产物

| 内容 | 路径 |
|---|---|
| 每个测试的完整 stdout + 命令 | `logs/<YYYYmmdd-HHMMSS>-gs-usb-<test>.log`（自动生成，直接提交） |
| 矩阵汇总 | `logs/<ts>-gs-usb-matrix.log` |
| dmesg 异常全文 | `/tmp/gs_usb_artifacts/dmesg-full-<test>-<ts>.log`（异常时才有） |
| 捕获的原始帧 / 时间戳 JSON | `/tmp/gs_usb_artifacts/<test>-<ts>/` |

## 8. 实测结果

见 `RESULTS.md`（本目录）。结构固定为"项目 / 命令 / 结果 / 关键数字 / 日志"，可直接粘进第 39 章表格。

## 9. 已知限制与待确认

| 项 | 说明 | 处理 |
|---|---|---|
| 终端电阻 | 板上是**手动拨码开关**，固件按约定不 advertise `GS_CAN_FEATURE_TERMINATION` | 测项 9 只验证"请求被拒绝（EOPNOTSUPP）"；电阻值本身无法软件设置 |
| 5M/8M 数据段 | 5M 已在 R8 实测通过（双向 0 丢帧、3.58 Mbit/s 载荷、25 万帧长测），细节见第 41 章 §41.7.8；8M 能跑但超 TCAN1044AV 标称，只记录不作档位 | 5M 用 `test_5m.sh`；旧固件卡死问题用 `repro_tx_urb_leak.sh --expect-stall` 复现 |
| 硬件时间戳精度 | 设备侧是 **1 MHz 计数器**（1 µs 量化） | 测项 5 会打印量化步长；1 µs 量化下的抖动不作为失败判据 |
| canplayer 回放 | 回放的时序由用户态决定，帧间隔不精确 | 只验证"回放能被收到"，不评价时序 |
| 与 cannelloni / slcan 的对比 | 未测（**) | 待固件后补：USB 侧延迟/吞吐/CPU 采样；cannelloni 已有第 28 章基线可对照 |
