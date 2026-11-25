# ptp_master —— 板作 IEEE 1588 PTP 主钟（E2E / two-step / 硬件时间戳）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能

与 `ptp_slave` 同一套组件，仅**角色不同**：编译时 `SLAVE_ONLY=FALSE`，板端作为主钟（grandmaster）
发 Sync/Follow_Up/Announce，并响应从钟的 Delay_Req；主机 `ptp4l -s`（clientOnly）作为从钟同步到板端。

用途：验证 **反方向**（TX 打戳产生 Sync/Follow_Up 的 t1、RX 打戳处理 Delay_Req 的 t4），
以及 BMCA 里"板端作为 GM"的数据集是否正确（`stepsRemoved=0`）。

## 2. 接线

同 `ptp_slave`：主机 `enp6s0`(192.168.2.1) ↔ 转换盒 ↔ YT8011AN ↔ ENET0（板 192.168.2.100）。

## 3. 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/app/ptp_master

# 板端先上板（后台占锁跑 62 s），主机随后作为从钟
bash scripts/hwrun.sh samples/app/ptp_master --secs 62 \
     --send "sleep 1000;ptpbench 55;ptp" --lock-wait 1800 &
sleep 8
sudo host/ptp/run_ptp_slave.sh start      # ptp4l -i enp6s0 -m -E -H -s
sleep 45
sudo host/ptp/run_ptp_slave.sh stats      # 解析主机侧 master offset 并打印统计
sudo host/ptp/run_ptp_slave.sh stop
```

> 注意：板端上板期间会独占板锁，主机侧命令不要再用 `hwrun`；
> 若要让两者严格同步起停，可用 `hwrun.sh --post "timeout 60 host/ptp/run_ptp_slave.sh start-and-stats"`。

## 4. 期望现象

- 板端串口：`[ptp] ready ...` 后先 `PTP_LISTENING`，等公告接收超时（announceReceiptTimeout=3 × 2 s）
  自任主钟后进入 `state PTP_MASTER`（约数秒，实测值待补），之后每 1 s 打印一次 `updateClock` 行
  （`one-way delay averaged (E2E)` 在主钟侧为 0）；
- 板端 `ptp`：`state=MASTER`、`stepsRemoved=0`、`slaveOnly=false`；
- 主机 `ptp4l -m`：出现 `port 1 (enp6s0): ... to SLAVE`，并每秒打印
  `master offset <x> s2 freq <y> path delay <z>`；
- `run_ptp_slave.sh stats` 输出的 `master offset` 均值应在亚微秒量级。

## 5. 实测

**日志**：`logs/20261030-174216-ptp_master.log`（板端）、
`logs/20261030-174216-host-ptp4l-slave.log`（主机 ptp4l -s）。

命令（一次持锁 ≤90 s，主机测试用 `--post` 在持锁期间跑）：

```bash
bash scripts/hwrun.sh samples/app/ptp_master --secs 20 --send "sleep 1000;ptp" \
     --post "timeout 80 bash host/ptp/run_ptp_slave.sh bench 42" --lock-wait 1800
```

| 指标 | 实测值 |
|---|---|
| 板端状态 | `state=MASTER slaveOnly=false prio1=100 stepsRemoved=0`，GM=`000000.fffe.00d00e`（板 MAC 的 EUI-64） |
| 主机进入 SLAVE | 板端起 Announce 后约 2 s |
| **主机侧 offset（剔除前 5 个采样，n=28）** | **均值 +98.2 ns，σ 259.7 ns**，min −33 ns，max 1126 ns，p50 2 ns，p99 1126 ns |
| 单向路径延迟 | **13840.2 ns ± 24.1 ns** |
| 收敛 | 连续 5 点 `|offset|<1 µs` 在第 6 个采样（≈6 s） |
| 判据 | `|mean| ≤ 1000 ns` 且 `σ ≤ 500 ns` → **PASS** |

主机日志片段：

```text
ptp4l[116180.550]: selected best master clock 000000.fffe.00d00e
ptp4l[116184.550]: port 1 (enp6s0): UNCALIBRATED to SLAVE on MASTER_CLOCK_SELECTED
ptp4l[116211.551]: master offset         -6 s2 freq  +18280 path delay     13838
ptp4l[116212.551]: master offset          2 s2 freq  +18286 path delay     13838
```

> **关键**：板端必须用 `-DDEFAULT_PRIORITY1=100` 编译（CMakeLists 已设），否则主机 BMCA 认为
> 自己（priority1=128）更优，会一直打印 `master state recommended in slave only mode`，永不进 SLAVE。

## 6. 易错点

与 `ptp_slave` 相同的 5 条（见其 README §6）。补充两条主钟方向特有的：

1. **板端要先上板再起主机从钟**：`ptp4l` 若先起来且长时间收不到 Announce，会走
   `ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES` 自任主钟（两边都当主钟就同步不上）；
   所以命令里用 `-s`（clientOnly）强制主机从钟。
1b. **优先级**：板端 clockClass=248、priority1 默认 248，比主机 ptp4l 的 128 差 → 必须把板端
   priority1 调到 100（本 README 对应的 CMakeLists 已设），否则主机不会认主。
1c. **PHC 复位**：主机做过从钟后 `/dev/ptp1` 会被改成板端时间（2022 年），再让主机当主钟
   就会把 2022 年发给对端。切角色前先 `sudo phc2sys -s CLOCK_REALTIME -c /dev/ptp1 -O 0 -m` 复位。
2. **主钟的起始时刻**：板端 PTP 时钟初值是 1651074120（2022 年），主机同步后会跟随这个绝对时间，
   `date` 会"倒退"。需要合理绝对时间时用 `ptpreset <sec>` 先设成当前 epoch。

## 7. 相关

- 组件：`components/app_ptp/`；上位机：`host/ptp/`；书稿：`docs/chapters/38-ptp-time-sync.md`