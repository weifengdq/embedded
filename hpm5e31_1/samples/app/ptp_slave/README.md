# ptp_slave —— 板作 IEEE 1588 PTP 从钟（E2E / two-step / 硬件时间戳）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能

hpm5e31_1 的 ENET0（RGMII + YT8011AN 1000BASE-T1）跑 **lwIP 2.1.2 + PTPd 2.0.1 移植版**，
由主机 `ptp4l` 当主钟，板端作为普通时钟（Ordinary Clock）从钟同步到主钟。
时间戳全部来自 **ENET MAC 硬件**（RX 描述符 RDES6/RDES7、TX 描述符 TDES6/TDES7 + TTSE），
协议栈与时钟调整见 `components/app_ptp/README.md`。

## 2. 接线与网络

```text
主机 enp6s0 (192.168.2.1/24, 1000Mb/s Full，硬件时间戳 /dev/ptp1)
   │  RJ45
   ▼
1000BASE-T1 ↔ RJ45 转换盒（拨码 1000M Master）
   │  单对差分线
   ▼
YT8011AN (1000BASE-T1, Slave, PHYADDR=1) ── RGMII ── HPM5E31 ENET0 (192.168.2.100/24)
```

主机侧检查（必须看到 hardware-transmit/receive 才是硬件打戳）：

```bash
ip addr add 192.168.2.1/24 dev enp6s0 2>/dev/null || true
sudo ethtool -T enp6s0 | head -12          # 应有 hardware-transmit / hardware-receive / hardware-raw-clock
ping -c 3 192.168.2.100
```

## 3. 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/app/ptp_slave

# 1) 主机当主钟（硬件时间戳，E2E）
sudo host/ptp/run_ptp_master.sh start
# 2) 板端上板并测 55 s（板内 ptpbench 期间保持网络轮询）
bash scripts/hwrun.sh samples/app/ptp_slave --secs 62 --send "sleep 1000;ptpbench 55;ptp" --lock-wait 1800
# 3) 统计板端 offset
python3 host/ptp/ptp_stats.py --log $(ls -t logs/*-ptp_slave.log | head -1)
sudo host/ptp/run_ptp_master.sh stop
```

## 4. shell 命令

| 命令 | 说明 |
|---|---|
| `ptp` | 打印状态机状态/域/延迟机制/硬件时钟/offset/路径延迟/漂移/GM |
| `ptpstats` | 板上 offset 统计（n/均值/σ/min/max/last，只在进入 SLAVE 且延迟已知后 5 s 才计入） |
| `ptpstats reset` | 清空统计 |
| `ptpbench <secs>` | 阻塞式测量窗口，每 5 s 打印一次统计（默认 30 s） |
| `ptpreset <sec>` | 直接写 ENET 硬件 PTP 时钟（秒） |
| `sleep <ms>` | 延时但保持 PTP/网络轮询（板端 shell 单线程，普通 delay 会冻结协议栈） |

## 5. 实测

**主数据（60 s 连续窗口，2026-11-02）**：`logs/20261102-101345-ptp_slave.log`（`--secs 62 --send "sleep 1000;ptpbench 55;ptp"`）；
历史证据：`logs/20261030-170813-ptp_slave.log`（40 s 首测）、`logs/20261030-170507-ptp_slave.log`（修 transportSpecific 后首次测到路径延迟）、
`logs/20261030-170309-ptp_slave.log`（修复前 delay 恒 0）。

| 指标 | 60 s 窗口实测值 |
|---|---|
| 采集窗口 | 62 s 连续抓串口，板内 `ptpbench 55` |
| setTime → `PTP_SLAVE` | **12.0 s** |
| offset 稳态（剔除前 10 个，n=37） | **均值 −11.8 ns，σ 291.2 ns** |
| offset 稳态 min / max / p50 | −534 ns / **1265 ns** / 7 ns |
| offset 全锁定窗口（n=47） | 均值 −1194.2 ns，σ 3564.8 ns（含一次 13.8 µs 台阶） |
| 单向路径延迟（E2E） | **13838.8 ns ± 0.7 ns** |
| 观测频率漂移 | −25594.8（=−0.3905 ppm，板端时钟偏快） |
| 板端 `ptpstats` 快照 | `n=32 mean=+35.3 ns std=430.3 ns min=-534 ns max=1681 ns` |
| `ptp` 快照 | `state=SLAVE offset=9 ns pathdelay=13839 ns stepsRemoved=1 mech=E2E` |
| 判据 | `|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns` → **PASS** |

串口日志片段（PTPd 的调试行，1 s 一次）：

```text
(D 1793351183.273238081) updateClock: one-way delay averaged (E2E):           0s       13841ns
(D 1793351183.274378141) updateClock: offset from master:               0s          -9ns
(D 1793351183.275431401) updateClock: observed drift:              -18814
```

`ptpstats`（板上统计）输出形态：

```text
[ptpstats] n=24 mean=-15.5 ns std=363.0 ns min=-463 ns max=1350 ns last=-46 ns
[ptpbench] done (40 s)
[ptp] state=SLAVE domain=0 mech=E2E slaveOnly=t
```

## 6. 易错点（详见第 36 章 §8）

1. **PTPd 上游把 transportSpecific 写成 8** → linuxptp 丢弃板端 Delay_Req，路径延迟恒 0。
   本仓库的 `components/app_ptp/ptpd_v2` 已修（默认 0）；用上游未修版时主机需 `ignore_transport_specific 1`。
2. **第一次 Delay_Resp 会给 offset 一次 13.8 µs 台阶**，统计必须跳过（`ptpstats` 已内置 5 s 稳定窗）。
3. **不要用忙等延时**：`hwrun --send` 没有内建 sleep，用 `sleep <ms>`/`ptpbench`。
4. **`bsp_uptime_ms()` 在每轮 <1 ms 的循环里会冻结**（整数截断），不要用它做超时。
5. 全量 62 s 抓串口时，主机 `ptp4l` 必须先起来，否则板端会停在 `PTP_LISTENING` 等 Announce。

## 7. 相关

- 组件：`components/app_ptp/`（`ptpd_v2/` 为 PTPd 2.0.1 移植版）
- 上位机：`host/ptp/`（`run_ptp_master.sh`、`ptp_stats.py`、`run_e2e_test.sh`）
- 书稿：`docs/chapters/38-ptp-time-sync.md`