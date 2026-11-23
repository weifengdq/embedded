# gptp_demo —— gPTP 风格（P2P 逐跳延迟 + 125 ms Sync）从钟

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能与定位

IEEE 802.1AS（gPTP）与 IEEE 1588 默认 profile 的主要差别：

| 项 | IEEE 1588 默认（E2E） | 802.1AS（gPTP） | 本例程 |
|---|---|---|---|
| 延迟机制 | E2E（端到端） | **P2P（逐跳）** | P2P（`DEFAULT_DELAY_MECHANISM=2`） |
| Sync 间隔 | 1 s（logSyncInterval=0） | **125 ms（−3）** | 125 ms（`DEFAULT_SYNC_INTERVAL=-3`） |
| 主钟选举 | BMCA + Announce | BMCA + Signaling（无 Announce） | 仍是 PTPd 的 Announce 实现 |
| 域 | 0 | 0（默认）/1 | 0 |

所以本例程是"**参数接近 802.1AS 的 P2P 从钟**"，不是完整的 802.1AS 实现（无 Signaling、
无 gPTP 的端口状态机细节）。它的价值是：用 P2P 验证**逐跳延迟测量**通路
（Pdelay_Req/Pdelay_Resp/Pdelay_Resp_Follow_Up），为后续接真正的 gPTP 交换机打基础。

## 2. 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/app/gptp_demo

# 主机用 P2P 模式当主钟（-P），板端 125 ms Sync 从钟
sudo host/ptp/run_ptp_master.sh start          # 默认 E2E
sudo pkill -INT -x ptp4l
sudo sh -c 'nohup ptp4l -i enp6s0 -m -P -H > /tmp/ptp4l-p2p.log 2>&1 &'
sleep 3
bash scripts/hwrun.sh samples/app/gptp_demo --secs 40 --send "sleep 1000;ptpbench 32;ptp" --lock-wait 1800
python3 host/ptp/ptp_stats.py --log $(ls -t logs/*-gptp_demo.log | head -1)
sudo pkill -INT -x ptp4l
```

对端 Pdelay 目标：`ptp4l -P` 与板端都会向 `224.0.0.107`（P2P 组播）发 Pdelay 报文。

## 3. 期望现象

- 板端 PTPd 打印 `updateClock: one-way delay averaged (P2P): 0s <x>ns`（E2E 里这行是 E2E）；
- Sync 频率 8 次/s（125 ms），日志行密度是 `ptp_slave` 的 8 倍；
- `ptp` 显示 `mech=P2P`；
- 主机 `ptp4l -P` 侧 `path delay` 与板端 P2P 延迟同量级（单跳 RGMII + 转换盒）。

## 4. 实测

**主数据（60 s 连续窗口，2026-11-02）**：`logs/20261102-101509-gptp_demo.log`；
主机 `ptp4l -i enp6s0 -m -P -H`（P2P 主钟）。历史：`logs/20261030-174427-gptp_demo.log`（40 s 首测）。

| 指标 | 60 s 窗口实测值 |
|---|---|
| 采集窗口 | 62 s 连续抓串口，板内 `ptpbench 55` |
| 板端状态 | `state=SLAVE mech=P2P stepsRemoved=1`，GM=`C0A2B6..A92C`（主机 enp6s0） |
| setTime → `PTP_SLAVE` | **3.0 s**（P2P 无 Announce 竞争，比 E2E 的 12 s 快得多） |
| **P2P 单向路径延迟** | **13840.3 ns ± 1.0 ns** |
| **offset 稳态（剔除前 10 个，n=47）** | **均值 −154.6 ns，σ 242.9 ns**，min −1150 ns，max 1 ns，p50 −56 ns |
| offset 全锁定窗口（n=57） | 均值 −599.6 ns，σ 2392.4 ns，min −16112 ns，max 2746 ns |
| 观测频率漂移 | −25093.3（=−0.3829 ppm） |
| 判据 | `|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns` → **PASS** |

板端日志片段：

```text
[ptp] state=SLAVE domain=0 mech=P2P slaveOnly=t
(D 1651074459.377328416) updateClock: one-way delay averaged (P2P):           0s       13841ns
(D 1651074460.378479176) updateClock: offset from master:               0s         -14ns
```

**结论**：60 s 窗口下 P2P 路径延迟 σ=1.0 ns、E2E σ=0.7 ns，都是 ns 级稳定；P2P 的真正优势是
**锁定快（3 s vs 12 s）**和**不依赖对端 GM 的后续报文（Follow_Up）**——这正是 802.1AS 选 P2P 的原因。
（2026-11-02 起板端 `ptpstats` 在 P2P 下也能统计：路径延迟取 `portDS.peerMeanPathDelay` 而非
`currentDS.meanPathDelay`，此前 P2P 会一直显示 "no sample yet"。）

## 5. 坑

1. **E2E 与 P2P 不能混用**：一端 `-P` 一端 `-E` 时，双方各发各的 Delay_Req/Pdelay_Req，
   都收不到对应响应，现象是"一直不锁定、delay 恒 0"；
2. P2P 的组播地址是 `224.0.0.107`（不是 `224.0.1.129`），防火墙/IGMP 要放行两者；
3. 125 ms Sync 下串口日志量 ×8，长测时建议把 `PTPD_DBG` 关掉（`constants_dep.h`）以免日志淹没；
4. 其余与 `ptp_slave` 相同（transportSpecific、忙等、`bsp_uptime_ms` 冻结等）；
5. 做完"主机当从钟"的实验后，主机 `/dev/ptp1` 会被改成对端时间，再当主钟前要
   `sudo phc2sys -s CLOCK_REALTIME -c /dev/ptp1 -O 0 -m` 复位（第 36 章坑 8）。

## 6. 相关

- 书稿：`docs/chapters/38-ptp-time-sync.md` §2.4（E2E vs P2P）、§6.5
- 组件：`components/app_ptp/`；上位机：`host/ptp/`