# 测试例子集（可直接复制执行）

约定：
* 主机 = Ubuntu（`enx001ec0d1c337`，192.168.10.10，PLCA 协调者 ID 0）
* 板子 = STM32H723(.11) / HPM(.12) / S32K(.13) / TC387(.14) / RK3506(.15)
* 板端命令通过串口输入（`./tools/s25.py --auto h723 -c "..."`；`stm32` 是同一 STLINK 口的别名）

---

> **本篇是"照着敲"的**：每个例子都包含前置条件、命令、期望输出、失败怎么查。
> 全部例子假设板端命令通过 `./tools/s25.py --auto <board> -c "..."` 发送。

## 本篇目录

| 例子 | 内容 | 需要什么 |
| --- | --- | --- |
| 1 | 确认 SPI 与芯片 | 单板 |
| 2 | 单节点连通（CSMA/CD） | 单板 + 主机 |
| 3 | PLCA 主从（主机协调者） | 单板 + 主机 |
| 4 | 板子当协调者 | 单板 + 主机 |
| 5 | 多节点轮询 | 多板 + 主机 |
| 6 | 协调者冗余（CR） | 2 板 + 主机 |
| 7 | PLCA 诊断位（故障注入） | 1–2 板 |
| 8 | SQI / 信号质量 | 单板 + 主机 |
| 9 | HDD 线束检测 | 单板（总线静默） |
| 10 | 拓扑发现（测距） | 2 板（总线静默） |
| 11 | 吞吐测试 | 单板 + 主机 |
| 12 | 最小帧 / 突发 | 单板 + 主机 |
| 13 | 时间戳 / PPS | 单板 |
| 14 | TC10 睡眠 / 唤醒 | 2 板 |
| 15 | LED 验证 | 单板 |
| 16 | 一键验收 | 全部 |
| 17 | 从零复现一次交付验收 | 全部 |
| 18 | **板子当 PLCA 协调者 (ID=0)** | 单板 + 主机 |
| 19 | TC6 设备时间（读 / 写 / 单调性） | 单板 |
| 20 | TC6 发送时间戳（TTSCA/B/C） | 单板 |
| 21 | TC14 拓扑发现（静默总线 + 手动寄存器） | 2 板 |
| 22 | DIO / LED 功能码切换 | 单板 |
| 23 | SQI / HDD 多板对比 | 多板 |

---

## 例 1：确认 SPI 与芯片（不需要总线）

```bash
./tools/s25.py --auto h723 -c "s2500 reg 0 0"
# 期望: R mms0[0x0000] = 0x00000011 (17)
./tools/s25.py --auto h723 -c "s2500 info"
./tools/s25.py --auto h723 -c "s2500 selftest"
```

`s2500 selftest` 期望：

```
[1] SPI/寄存器   : PASS (IDVER=0x00000011)
[2] PHY ID       : 0x180FF411  MAC=xx:xx:xx:xx:xx:xx
[3] PCS 环回收发 : PASS
[4] PHY 环回收发 : PASS
[5] PHY Status   : 0x0809 link=1
[6] SQI/SQI+     : 7/7, 255/255
[7] HDD          : valid=1 SHORT_OPEN=0 (OK) 1W=0
```

---

## 例 2：单节点连通（CSMA/CD）

```bash
# 主机 PLCA 关闭（纯 CSMA）
sudo ./host/plca.sh off
# 板端默认就是 CSMA
ping -c 5 192.168.10.11
```

对照实验：LAN8651 旧工程在 CSMA 下 TCP 只有 6.72 Mbit/s，PLCA 下 8.86 Mbit/s。

---

## 例 3：PLCA 主从（主机协调者，板跟随者）

```bash
# 1) 主机成为协调者
sudo ./host/plca.sh on
ethtool --get-plca-cfg enx001ec0d1c337      # Enabled: Yes, node ID 0

# 2) 板端加入 PLCA（ID=1）
./tools/s25.py --auto h723 -c "s2500 plca id 1 ncnt 8 totmr 32 en 1"
./tools/s25.py --auto h723 -c "s2500 status"
# 期望: plca : ON id=1 ncnt=8 PST=1
```

**PST=0 的排查**：主机是否真的在发 BEACON（`plca.sh status` 的 Status: on）、
NCNT/TOTMR 是否一致、跟随者 ID 是否 < NCNT。

---

## 例 4：板子当协调者（反向主从）

```bash
# 主机改成跟随者 ID=1
sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 1 node-cnt 8 to-tmr 32
# 板子成为协调者
./tools/s25.py --auto h723 -c "s2500 plca id 0 en 1"
./tools/s25.py --auto h723 -c "s2500 status"      # PST=1
ping -c 5 192.168.10.11
```

---

## 例 5：多节点轮询（5 块板同时在线）

```bash
for i in 11 12 13 14 15; do ping -c 3 -i 0.02 192.168.10.$i & done; wait
./host/bus_test.py soak 11 1000        # 1000 次 ping 的丢包/RTT
```

期望：PLCA 下 0 丢包；RTT 随节点数增加但稳定（无指数退避）。
对比：CSMA 下 8 节点时延抖动明显、丢包上升。

---

## 例 6：协调者冗余（CR）

```bash
# 板 2 开启冗余，NCNT 必须与协调者一致
./tools/s25.py --auto hpm5361 -c "s2500 plca id 2 ncnt 8 en 1 cr 1"

# 主机停止发 BEACON
sudo ./host/plca.sh off
./tools/s25.py --auto hpm5361 -c "s2500 status"
# 期望: CRPS=1（板子在代发 BEACON），其它板仍能通信

# 恢复
sudo ./host/plca.sh on
./tools/s25.py --auto hpm5361 -c "s2500 status"   # CRPS 回到 0
```

---

## 例 7：PLCA 诊断位（故障注入）

```bash
# 7.1 双协调者 → UNEXPB
./tools/s25.py --auto h723 -c "s2500 plca id 0 en 1"
./tools/s25.py --auto h723 -c "s2500 status"     # plca diag bit1 = 1

# 7.2 重复 ID → RXINTO
./tools/s25.py --auto h723 -c "s2500 plca id 3 en 1"
./tools/s25.py --auto s32k312 -c "s2500 plca id 3 en 1"
./tools/s25.py --auto h723 -c "s2500 status"     # bit2 = 1

# 7.3 NCNT 太小 → BCNBFTO
./tools/s25.py --auto h723 -c "s2500 plca id 1 ncnt 2 en 1"
./tools/s25.py --auto h723 -c "s2500 status"     # bit0 = 1
```

恢复：`s2500 plca id <正确ID> ncnt 8 en 1`。

---

## 例 8：SQI / 信号质量

```bash
./tools/s25.py --auto h723 -c "s2500 sqi"
# 期望: SQI=7/7 SQI+=255/255
```

对比实验（验证终端电阻影响）：
1. 正常两端终端：SQI≈7，SQI+≈200-255；
2. 多接一块带终端的板：SQI 下降，SQI+ 明显降低；
3. 拔掉一端终端：SQI 下降但通常仍可通信（HDD 会报 OPEN）。

---

## 例 9：HDD 线束检测

```bash
# 总线必须静默：先让主机停 BEACON，所有板停止发送
sudo ./host/plca.sh off
./tools/s25.py --auto h723 -c "s2500 hdd"
# 正常: SHORT_OPEN=0
# 拔掉一端终端后重跑: SHORT_OPEN=1
# 短接 P/N 后重跑: SHORT_OPEN=2
```

---

## 例 10：拓扑发现（测距）

需要两个支持 TC14 的节点（两块 S2500 板）：

```bash
# 被测节点先启动（板 B）
./tools/s25.py --auto s32k312 -c "s2500 td meas 8"
# 参考节点启动（板 A），测量约 1-2 s
./tools/s25.py --auto h723 -c "s2500 td ref 8"
# 期望: dist_mr 与线长对应（1 m 约 5 ns 往返）
```

注意：测量期间总线必须静默（关闭 PLCA 协调者 BEACON）。

---

## 例 11：吞吐测试

```bash
# 板端起 iperf2 服务端
./tools/s25.py --auto h723 -c "iperf -s"
# 主机 -> 板（下行）
iperf -c 192.168.10.11 -t 10 -i 5 -f m
# UDP
iperf -c 192.168.10.11 -u -b 10M -l 1400 -t 10
# 板 -> 主机（上行）: 主机先跑 iperf -s，再让板端作客户端 iperf -c 192.168.10.10 -t 10
# 或者
./host/bus_test.py iperf 11
```

参考目标（2026-11-20 实测）：硬件 SPI 平台 **6.26–9.07 Mbit/s**（STM32H723/HPM/TC387/RK3506，
S32K312 的 LPSPI0 为 6.26）；软件 SPI 回退时约 1.4。注意 lwiperf **不支持 `iperf -R`**，测上行要板端当客户端。

---

## 例 12：最小帧 / 突发

```bash
iperf -c 192.168.10.11 -l 64 -t 10 -i 5      # 64B 小包
./tools/s25.py --auto h723 -c "s2500 bench tx 1000 100"
# 期望: 打印 fps 与 Mbit/s
```

---

## 例 13：时间戳 / PPS

```bash
./tools/s25.py --auto h723 -c "s2500 time"
./tools/s25.py --auto h723 -c "s2500 time set 1000 0"
./tools/s25.py --auto h723 -c "s2500 ts a"     # 发送时间戳（需 FTSE=1 且发送帧带 TSC）
# PPS: 比较模块输出 1 Hz 到 DIO0
./tools/s25.py --auto h723 -c "s2500 reg 12 4120 1000000"   # CMPPER = 1 ms 半周期 → 2 ms?
```

> 具体 CMPCTL/CAPCTL 用法见 `docs/04_register_map.md` 与数据手册。

---

## 例 14：TC10 睡眠 / 唤醒

```bash
# 进入睡眠（SPI 将无响应）
./tools/s25.py --auto h723 -c "s2500 sleep"
# 由主机发送唤醒音（主机作为 PLCA 协调者）
#   注意：主机侧驱动暂无 wake tone 接口，可用另一块板：
./tools/s25.py --auto s32k312 -c "s2500 sleep wake"
# 板子唤醒后重新上电初始化
./tools/s25.py --auto h723 -c "s2500 info"     # WS_STATUS1.WUT=1
```

---

## 例 15：LED 验证

```bash
./tools/s25.py --auto h723 -c "s2500 led 15 5"   # ACT=TX&RX, LINK=PLCA
# 目视：黄灯随流量闪，绿灯在 PLCA 同步后常亮
```

---

## 例 16：一键验收

```bash
./host/acceptance.sh docs/results
# 输出: PLCA 配置、各板 ping、iperf 双向、ARP
```

---

## 例 17：从零复现一次交付验收

把 [`docs/14_delivery_checklist.md`](14_delivery_checklist.md) 的 7 个阶段串起来跑一遍，
适合交付前自检（保存为 `/tmp/accept.sh` 后 `bash /tmp/accept.sh hpm 192.168.10.12`）：

```bash
BOARD=$1 ; [ -z "$BOARD" ] && BOARD=hpm
IP=$2    ; [ -z "$IP" ]    && IP=192.168.10.12

echo "=== 阶段 1: 设备 ==="
lsusb | grep -E "0483:3754|1366:0105|0403:6010|058b:0043|184f:0051"
iperf --version | head -1          # 必须是 2.1.x

echo "=== 阶段 2: 无硬件检查 ==="
make -C common/s2500 check
gcc -std=c11 -Icommon/s2500 tools/sim/s2500_sim.c common/s2500/s2500.c \
    -o /tmp/sim && /tmp/sim

echo "=== 阶段 3: 单板自检 ==="
./tools/s25.py --auto "$BOARD" -c "s2500 reg 0 0"
./tools/s25.py --auto "$BOARD" -c "s2500 info"
./tools/s25.py --auto "$BOARD" -c "s2500 selftest"

echo "=== 阶段 4: 总线与 PLCA ==="
sudo ./host/setup_usb10t1s.sh
ethtool --get-plca-status enx001ec0d1c337
ping -c 10 -i 0.05 "$IP"

echo "=== 阶段 5: 吞吐与诊断 ==="
./tools/s25.py --auto "$BOARD" -c "s2500 sqi"
./tools/s25.py --auto "$BOARD" -c "s2500 hdd"
iperf -c "$IP" -t 10 -i 5 -f m

echo "=== 阶段 6: 文档与 PDF ==="
ls docs/*.md | wc -l
# 总 PDF 可选：用 pandoc 自行合并 docs/*.md
```

期望输出对照见 [`docs/14_delivery_checklist.md`](14_delivery_checklist.md) 第 3 节。

## 例 18：板子当 PLCA 协调者（ID=0）

平时都是主机（USB-10T1S）当协调者。要验证**板子能否当协调者发 BEACON**，用
`host/coord_test.sh`（本轮 **5/5 平台通过**）：

```bash
./host/coord_test.sh h723         # 或 hpm / tc387 / rk3506 / s32k
```

脚本内部做的事（也可以手工执行）：

```bash
# 1) 主机降为跟随者 ID 7
sudo ethtool --set-plca-cfg enx001ec0d1c337 enable on node-id 7 node-cnt 8 to-tmr 32

# 2) 被测板升为协调者 ID 0
./tools/s25.py --auto hpm -c "s2500 plca id 0 ncnt 8 totmr 32 en 1"
./tools/s25.py --auto hpm -c "s2500 status"        # 期望 PST=1

# 3) 双向验证
ping -c 20 -i 0.05 192.168.10.12                   # 主机 -> 板
./tools/s25.py --auto hpm -c "ping 192.168.10.10 20"   # 板 -> 主机

# 4) 吞吐（主机 iperf 服务端 + 板端客户端）
iperf -s &
./tools/s25.py --auto hpm -c "iperf -c 192.168.10.10 -t 10"

# 5) 恢复：板回原 ID，主机回 ID 0
./tools/s25.py --auto hpm -c "s2500 plca id 2 ncnt 8 en 1"
sudo ./host/setup_usb10t1s.sh
```

实测结果（2026-11-20）：

| 被测节点 | PST | 双向 ping | TCP 下行 | TCP 上行 |
| --- | --- | --- | --- | --- |
| HPM5361 | 1 | 20/20，0% | 8.80 | 7.24 |
| TC387 | 1 | 20/20，0% | 7.43 | 8.87 |
| RK3506 | 1 | 20/20，0% | 8.93 | 9.00 |
| S32K312 | 1 | 20/20，0% | 6.26 | 7.75 |
| STM32H723 | 1 | 20/20，0% | 9.07 | 8.87 |

> ⚠️ 同一时刻**只能有一个协调者**。切换顺序必须是「先让板升为协调者，再把主机降级」，
> 反了会有短暂无 BEACON 的窗口。测完务必恢复，否则别人测试会用错协调者。

---

## 高级特性实测（2026-11-20）

> 本节是 2026-11-20 在 HPM5361 / TC387 / S32K312 上的实测记录，原始数据见
> [`docs/results/2026-11-20_features.md`](results/2026-11-20_features.md)。
> 板端命令仍用 `./tools/s25.py --auto <board> -c "..."`。

## 例 19：TC6 设备时间（读 / 写 / 单调性）

```bash
# 连读 5 次，看是否单调
./tools/s25.py --auto hpm -c "s2500 time" -c "s2500 time" -c "s2500 time" -c "s2500 time" -c "s2500 time"
# 写时间并回读
./tools/s25.py --auto hpm -c "s2500 time set 1000 0" -c "s2500 time"
```

实测（HPM5361）：

```
PTP time: 1779.548507384 s
PTP time: 1779.926991304 s
PTP time: 1780.296795944 s
PTP time: 1780.662814824 s
PTP time: 1781.035877864 s

$ s2500 time set 1000 0
time set to 1000.000000000
PTP time: 1000.367923752 s
# sleep 2 后
PTP time: 1002.728833112 s
```

结论：✅ 单调递增、写生效、速率与主机一致（<1% 读数误差）。

## 例 20：TC6 发送时间戳（TTSCA/B/C）

原固件没有给发送帧置 TSC 的入口，本次新增测试命令（HPM 已烧写）：

```bash
./tools/s25.py --auto hpm -c "s2500 ts tx a 60" -c "s2500 reg 0 0x10"
./tools/s25.py --auto hpm -c "s2500 ts tx b 60" -c "s2500 reg 0 0x12"
./tools/s25.py --auto hpm -c "s2500 ts tx c 60" -c "s2500 ts c"
```

实测输出：

```
$ s2500 ts tx a 60
ts tx: rc=0 len=60 tsc=0 status0=0x00000440 avail=0 TTSCA=0x0000000000000000 sec=0 ns=0
R mms0[0x0015] = 0x23467BB8        # 时间戳实际落在 TTSCCL
R mms0[0x0010] = 0x00000000        # TTSCA 始终 0
R mms0[0x0012] = 0x00000000        # TTSCB 始终 0
```

解码：`0x23467BB8` = 秒字段 0、纳秒 591821752 → 设备时间 148.5918 s（发送前读 148.2901 s）；
`ts tx b` 得到 151.4469 s、`ts tx c` 得到 154.3040 s，都落在命令窗口内。

**异常**：TSC=1/2/3 都写进 TTSCC，TTSCA/B 不填充（STATUS0 恒 = TTSCAC）。
结论：⚠️ 发送时间戳能用（读 TTSCC），A/B 寄存器路径**未通过**，疑为数据头 TSC 位域与
`docs/03_spi_protocol.md` 的 bit7:6 定义不一致，需示波器/FAE 确认。

**坑**：
* 不打开 `CONFIG0.FTSE` 或帧头 `TSC=0`，TTSC 一定全 0（已实测 ping 20 帧后仍为 0）；
* 打开 FTSE 会让**接收帧也前插时间戳**，当前 `s2500_recv` 不解析 RTSA，
  测试窗口要短、发完立即复原 CONFIG0，否则 lwIP 会收错帧。

## 例 21：TC14 拓扑发现（必须静默总线 + 手动寄存器）

```bash
# 1) 停主机 BEACON（测完务必恢复）
sudo ./host/plca.sh off
# 2) 被测节点先起: 0x9E80 = TD_EN|DM_DUR=15(16ms)|AUTO_START, REFN=0
./tools/s25.py --auto tc387 -c "s2500 reg 4 0xCE00 0x9E80"
sleep 0.3
# 3) 参考节点后起: 0xDE80 = 同上 + REFN
./tools/s25.py --auto hpm -c "s2500 reg 4 0xCE00 0xDE80"
sleep 0.5
# 4) 读结果
./tools/s25.py --auto hpm -c "s2500 reg 4 0xCE01" -c "s2500 reg 4 0xCE02" -c "s2500 reg 4 0xCE04" -c "s2500 reg 4 0xCE06" -c "s2500 reg 4 0xCE08"
# 5) 两端退出、恢复 BEACON
./tools/s25.py --auto hpm   -c "s2500 reg 4 0xCE00 0"
./tools/s25.py --auto tc387 -c "s2500 reg 4 0xCE00 0"
sudo ./host/plca.sh on
```

实测（TC387 meas / HPM ref，dur=16 ms）：

```
HPM(ref)   : CE01=0xA000 (DLYM_DONE|DM_DONE) CE02=0x4966(18790) CE04=0x94D3(38099) CE06=0x9409(37897) CE08=0xF000(16ms)
TC387(meas): CE01=0xA000 CE02=0x4967(18791) CE04=0x9408(37896) CE06=0 CE08=0
```

反方向（HPM meas / TC387 ref）同样成功：DIST_MR 18749/18749、DLY_MR 37975/37878。

**结论**：✅ TC14 能用。`16ms/18750 ≈ 853 ns` 往返、`16ms/37950 ≈ 421 ns` 单端内部延时，
反推单程 tTOF ≈ 4.6–5.5 ns（约 0.9–1.1 m @5 ns/m），重复性 ±0.03%。

**坑（两个都踩过）**：
1. `s2500 td ref/meas` 原在 `DLYM_DONE` 就返回（`dist_mr=0`）；2026-11-20 已改为等 `DM_DONE` 并加 2*dur+10ms 稳定期，但封装命令阻塞轮询期间芯片会复位、`dist` 仍不稳定，**测距请用手动寄存器序列**；
2. 总线不静默（BEACON 还在）时单端跑 `s2500 td ref 8` 立刻返回 `dly=1`、`TD_STAT=0x4000`（DLYM_ERR），
   这就是之前“TC387 全 0”的原因，**不是芯片不支持**；
3. TD 实验后芯片可能回到上电默认态（`CONFIG0 SYNC=0`、PLCA off id=255、link down），
   `s2500 info` 能看出来，板端 `reboot` 恢复（MCU 重新拉 RST_N 并重配）。

## 例 22：DIO/LED 功能码切换

```bash
./tools/s25.py --auto hpm -c "s2500 reg 12 0x0012"          # 读 DIOCFG0
./tools/s25.py --auto hpm -c "s2500 led 15 5" -c "s2500 reg 12 0x0012"
./tools/s25.py --auto hpm -c "s2500 led 5 15" -c "s2500 reg 12 0x0012"
./tools/s25.py --auto hpm -c "s2500 led 8 12" -c "s2500 reg 12 0x0012"
./tools/s25.py --auto hpm -c "s2500 led off" -c "s2500 reg 12 0x0012"
```

实测：`0x0000CBDF`（默认 FN0=15/FN1=5）→ `0x00008B9F` → `0x00009F8B` → `0x00009991` → `0x00008181`。

功能码（数据手册 Table 11）：`0xF` TX&RX 10 Hz 闪烁（不含 BEACON，黄 ACT）；
`0x5` PLCA Status 电平（绿 LINK，PST=1 常亮）；`0x8` 25 MHz 时钟输出；`0xC` BEACON 1.5 ms 脉冲；`0` 高阻。
**结论**：✅ 寄存器可控、回读一致；⚠️ 只做了寄存器验证，未拍照，交付时目视确认黄闪/绿亮。

**坑**：`led` 命令会把 DIOCFG0 的 `PEN`（上/下拉使能）清 0（0xCBDF→0x8B9F），
默认配置由 `s2500_dio_defaults` 写回；对外观无影响，但对比寄存器时别以为是 FN 写错。

## 例 23：SQI / HDD 四板对比

```bash
# SQI: 空闲一次, 再并发 ping 时读
for ip in 12 14 13; do ping -c 40 -i 0.01 192.168.10.$ip & done
./tools/s25.py --auto hpm -c "s2500 sqi"

# HDD: 必须先停 BEACON 让总线静默
sudo ./host/plca.sh off
./tools/s25.py --auto hpm -c "s2500 hdd"
sudo ./host/plca.sh on
```

实测汇总：

| 板 | SQI 空闲 | SQI 忙 | SQI+ 忙 | HDD SHORT_OPEN | 1W_SHORT |
| --- | --- | --- | --- | --- | --- |
| HPM5361 | 0/7 | 3/7 | 119/255 | 2（多余终端） | 1 |
| TC387 | 0/7 | 3/7 | 119/255 | 2 | 1 |
| S32K312 | 0/7 | 3/7 | 119/255 | 2 | 0 |
| RK3506 | 受限（驱动无此接口） | — | — | — | — |
| STM32H723 | 0/7 | 4/7 | 151/255 | 2（多余终端） | 1 |

**结论**：SQI 空闲 0 是正常的（无流量不更新）；HDD 四板一致 `SHORT_OPEN=2`，是台面多板并联端接过量，
不是芯片故障；要验证 0 需只保留两端终端。

## 例子索引：按目的查

| 我想…… | 用哪个例子 |
| --- | --- |
| 确认板子 / 驱动是不是好的 | 1 |
| 确认总线和主机是不是好的 | 2、3、5 |
| 复现一个稳定的吞吐数字 | 11、12 |
| 检查线束 / 端接 | 8、9 |
| 验证 PLCA 的异常处理 | 6、7 |
| 验证板子能当协调者 (ID=0) | 18 |
| 验证 TC6 时间戳 / TC14 测距 / DIO / SQI / HDD | 19–23 |
| 验证低功耗 | 14 |
| 交付验收 | 16、17 |

## 常见错误与排查（所有例子通用）


| 现象 | 原因 | 处置 |
| --- | --- | --- |
| 命令回 `Command not Found` | 发了 `\r\n` | 只用 `s25.py`（只发 CR） |
| 期望 0x00000011 却读到 0xFFFFFFFF | SPI 没通 | 见 [`docs/00_porting_guide.md`](00_porting_guide.md) §9 |
| `PST=0` | 主机没发 BEACON / NCNT 不一致 | 见 [`docs/02_bus_plan.md`](02_bus_plan.md) §8 |
| `iperf3` 连不上 | 板端是 iperf2 | 换 `iperf` |
| HDD 一直在报错 | 端接过多 | 见 [`docs/02_bus_plan.md`](02_bus_plan.md) §6 |
| 测距结果离谱 | 总线不静默 | 关 BEACON 再测 |
| 睡眠后无响应 | 这是预期行为 | 用唤醒音 / WAKE 引脚唤醒 |
| 多板并发时数字很差 | 互相干扰 | 串行测 |

## 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 测试矩阵（全表） | [`docs/06_test_matrix.md`](06_test_matrix.md) |
| 命令手册 | [`docs/08_shell_commands.md`](08_shell_commands.md) |
| 交付验收清单 | [`docs/14_delivery_checklist.md`](14_delivery_checklist.md) |
| 排障决策树 | [`docs/10_hardware_troubleshooting.md`](10_hardware_troubleshooting.md) |

---

