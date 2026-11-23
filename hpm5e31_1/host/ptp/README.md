# host/ptp —— IEEE 1588 / linuxptp 上位机工具

板端（`samples/app/ptp_slave` / `ptp_master` / `gptp_demo`）跑 HPM SDK PTPd 2.0.1 移植版，
**时间戳全部来自 HPM5E31 ENET MAC 硬件**（RX/TX 描述符 RDES6/7、TDES6/7）。
本目录用 linuxptp（ptp4l / pmc）在主机侧当对端，并把两边日志变成可比较的统计表。

## 1. 依赖

```bash
sudo apt-get install -y linuxptp            # ptp4l 4.4 / pmc / phc2sys
sudo ethtool -T enp6s0                     # 确认主机网卡支持硬件时间戳
```

本机实测（`sudo ethtool -T enp6s0`）：

```text
Capabilities:
        hardware-transmit
        software-transmit
        hardware-receive
        software-receive
        software-system-clock
        hardware-raw-clock
Hardware timestamp provider index: 1
Hardware timestamp provider qualifier: Precise (IEEE 1588 quality)
Hardware timestamp source: MAC
```

`/dev/ptp1` 即 enp6s0 的 PHC（`ls /sys/class/net/enp6s0/device/ptp/` → `ptp1`）。
ptp4l 启动日志第一行 `selected /dev/ptp1 as PTP clock` 是硬件打戳生效的标志。

## 2. 文件

| 文件 | 作用 |
|---|---|
| `run_ptp_master.sh` | 主机当**主钟**（grandmaster）：`ptp4l -i enp6s0 -m -E -H`，start/stop/status/pmc |
| `run_ptp_slave.sh` | 主机当**从钟**（`-s` clientOnly）：验证板端 `ptp_master` 的反方向同步 |
| `ptp_stats.py` | 解析板端串口日志或 ptp4l 日志，输出 offset/路径延迟/漂移统计表 + PASS/FAIL |
| `run_e2e_test.sh` | 一键：起主机主钟 → 上板跑 ptp_slave → 自动统计板端与主机两份日志 |

## 3. 快速开始

```bash
cd hpm5e31_1
# 板作从钟（E2E，two-step）
sudo host/ptp/run_e2e_test.sh

# 单独起主机主钟，然后自己跑板端测试
sudo host/ptp/run_ptp_master.sh start
bash scripts/hwrun.sh samples/app/ptp_slave --secs 45 --send "sleep 1500;ptpbench 35" --lock-wait 1800
python3 host/ptp/ptp_stats.py --log $(ls -t logs/*-ptp_slave.log | head -1)
host/ptp/run_ptp_master.sh stop

# 板作主钟：主机当从钟
bash scripts/hwrun.sh samples/app/ptp_master --secs 45 --send "sleep 1500;ptpbench 35" --lock-wait 1800 &
sleep 6; sudo host/ptp/run_ptp_slave.sh start; sleep 30; sudo host/ptp/run_ptp_slave.sh stats
```

## 4. ptp_stats.py

```bash
python3 host/ptp/ptp_stats.py --log logs/xxxx-ptp_slave.log            # 板端日志（自动识别）
python3 host/ptp/ptp_stats.py --log /tmp/ptp4l-slave.log --kind host   # 主机 ptp4l 日志
python3 host/ptp/ptp_stats.py --log <log> --json                       # 机器可读
python3 host/ptp/ptp_stats.py --log <log> --expect-mean-ns 500 --expect-std-ns 200
```

判据（默认）：`|mean(offset)| <= 1000 ns` 且 `std(offset) <= 500 ns` → PASS。

## 5. 实测结果

| 方向 | 对端 | offset 均值 / σ | 单向路径延迟 | 日志 |
|---|---|---|---|---|
| 板作从钟（E2E，**60 s 连续窗口**） | 主机 `ptp4l -E -H` 作主钟 | 板端 **−11.8 ns / 291.2 ns**（剔除前 10 个，n=37）→ **PASS** | 13838.8 ± 0.7 ns | `logs/20261102-101345-ptp_slave.log` |
| 板作主钟（E2E） | 主机 `ptp4l -s -E -H` 作从钟 | 主机侧 **+98.2 ns / 259.7 ns**（剔除前 5 个，n=28）→ **PASS** | 13840.2 ± 24.1 ns | `logs/20261030-174216-host-ptp4l-slave.log` |
| gPTP/P2P（板作从钟，**60 s 连续窗口**） | 主机 `ptp4l -P -H` 作主钟 | 板端 **−154.6 ns / 242.9 ns**（剔除前 10 个，n=47）→ **PASS** | 13840.3 ± 1.0 ns | `logs/20261102-101509-gptp_demo.log` |

`bench` 子命令（配合 `hwrun --post`，在板子仍运行时测主机侧）：

```bash
# 板作主钟：抓完串口后仍持锁，主机起从钟测 42 s 并直接出统计
bash scripts/hwrun.sh samples/app/ptp_master --secs 20 --send "sleep 1000;ptp" \
     --post "timeout 80 bash host/ptp/run_ptp_slave.sh bench 42" --lock-wait 1800

# 板作从钟（gPTP/P2P 变体）：主机先当 P2P 主钟，再上板
sudo pkill -INT -x ptp4l; MECH=P2P sudo sh -c 'nohup ptp4l -i enp6s0 -m -P -H > /tmp/ptp4l-p2p.log 2>&1 &'
bash scripts/hwrun.sh samples/app/gptp_demo --secs 40 --send "sleep 1000;ptpbench 32;ptp" --lock-wait 1800
```

> **PHC 复位**：主机当过从钟后，`/dev/ptp1` 会被 ptp4l 改成对端时间（本板初值是 2022 年）。
> 切换角色前执行 `sudo phc2sys -s CLOCK_REALTIME -c /dev/ptp1 -O 0 -m`（几秒后 Ctrl-C），
> 否则主机当主钟时会把错误时间发给对端（见第 36 章坑 8）。

## 6. 常见问题

| 现象 | 原因 / 处理 |
|---|---|
| ptp4l 卡在 `LISTENING` | 只有自己一台设备，等 `ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES`（约 6~10 s）后自任主钟，正常 |
| 板端 `one-way delay averaged` 恒为 0 | 对端丢弃了板端 Delay_Req：早期 PTPd 把 transportSpecific 写成 8，linuxptp 按 0 过滤 → 本仓库移植版已修（见第 36 章坑 1） |
| `pmc: timed out` | pmc 默认走 /var/run/ptp4l 的 UDS，需要 ptp4l 正在运行且 `-m` 模式带 UDS（默认开） |
| 主机报 `failed to open /dev/ptp1` | 需要 root；或该网卡不支持硬件时间戳，改 `-S`（软件时间戳）对比 |