# host/j1939 —— J1939 上位机工具（Python + can-utils）

本目录提供 4 个上位机工具 + 3 个一键脚本，全部**自带 J1939 解码**：
python-can 4.x 已移除 `can.j1939` 模块，所以 `j1939_core.py` 重新实现了 ID/PGN 编解码、
TP 重组与 DM1 解码，只依赖标准库 + python-can。

## 依赖与准备

```bash
# python-can 已在 $HOME/pyenv 中
$HOME/pyenv/bin/python -c "import can; print(can.__version__)"   # 4.6.1

# vcan0（虚机自测用；接板时改用 Bench B 的 cannelloni 隧道，见工作区约定 §3）
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# 内核 can-j1939（hoststack_interop.py 需要）
sudo modprobe can-j1939
```

## 工具

### j1939_monitor.py —— 总线监视/解码

```bash
./run_monitor.sh vcan0 5
# 或：python3 j1939_monitor.py -i vcan0 --duration 5 --pgns FECA,F004 --raw
```

逐帧打印 PGN/SA/DA/优先级与信号换算，结束时输出 PGN 周期统计表、DM1 灯状态时间线、TP 重组统计。
真实输出（`logs/20261030-175400-host-j1939-monitor.log`）：

```
 0.556519  TP.DT            DT seq=1 SA=0x00 [8] 01 07 26 45 64 83 A2 C1
 ...
 1.260922  [TP-DONE] PGN=0x0EF00 (PGN_0x0EF00) SA=0x00 len=100 CRC32=0xB5A935FC
TP: bam=1 rts=0 done=1 bytes=100 errors=0 gap avg=50.373ms min=50.141ms max=50.484ms
RESULT: PASS
```

### j1939_tp_test.py —— BAM / RTS-CTS 测试

```bash
./run_selftest.sh vcan0          # 同一 vcan0 两个 socket 自发自收，4 项断言
python3 j1939_tp_test.py --mode listen -i vcan0 --duration 20      # 被动收板端 TP
python3 j1939_tp_test.py --mode send-bam -i vcan0 --bytes 500      # 发 BAM
python3 j1939_tp_test.py --mode send-rts -i vcan0 --bytes 500 --dest 0xF9
```

真实输出（`logs/20261030-175400-host-j1939-tp.log`）：

```
[selftest] two sockets on vcan0
  BAM 100B: PASS len=100 time=760.7ms goodput=131.5 B/s
  BAM 500B: PASS len=500 time=3628.5ms goodput=137.8 B/s
  RTS/CTS 100B: PASS len=100 time=18.4ms goodput=5433.7 B/s
  RTS/CTS 500B: PASS len=500 time=78.3ms goodput=6384.7 B/s
RESULT: PASS (4 PASS / 0 FAIL), peer: rx=178 cts=6 eoma=2 err=0
```

### dm1_decode.py —— DM1/DM2 解码

```bash
python3 dm1_decode.py -i vcan0 --duration 8
python3 dm1_decode.py --file ../logs/20261030-172059-j1939_dm.log   # 离线
```

输出灯状态、SPN/FMI/OC 表（FMI 带中文含义）与灯变化时间线。真实输出
（`logs/20261030-175400-host-j1939-dm1.log`）：

```
[DM1] PGN=0x0FECA SA=0x00 lamp=0x10 [AWL] flash=0 t=0.309s
   SPN=100     FMI=4  OC=1   | 电压低于正常
   >> lamp change @ t=0.309s: 0x10 [AWL]
[DM1] PGN=0x0FECA SA=0x00 lamp=0x11 [MIL+AWL] flash=0 t=0.619s
RESULT: PASS
```

### hoststack_interop.py —— 与 Linux 内核 can-j1939 栈互操作

```bash
sudo modprobe can-j1939
python3 hoststack_interop.py -i vcan0
```

结论（`logs/20261030-175400-host-j1939-kernel.log`）：内核的地址声明与 RTS 帧能被本解码器
正确解析；**完整 TP 互操作失败**——内核 `j1939_session_tx_cts()` 的 CTS 字段布局
（`dat[1]=允许包数、dat[2]=起始包号`）与 J1939-21 不同，按标准回的 CTS 被内核以
Abort reason=5 取消。脚本输出 PARTIAL 而不是 PASS，属于**已知差异**。

## 一键脚本

| 脚本 | 作用 |
|---|---|
| `run_selftest.sh` | 建 vcan0（若不存在）+ 跑 TP selftest |
| `run_monitor.sh` | 监视指定接口指定秒数 |
| `run_all.sh` | 依次跑 selftest / monitor / dm1 / 内核互操作，输出到 logs/ |
| `run_benchb.sh` | Bench B 联调：起 vcan0+cannelloni 隧道，抓板端 J1939 帧并解码 |

所有脚本默认用 `$HOME/pyenv/bin/python`（可用 `PY=...` 覆盖），不要用系统 python3（没有 python-can）。

## 接板（Bench B）

板端烧 **J1939 GW 变体**：`J1939_WITH_GW=1 bash scripts/build.sh samples/app/j1939_ecu`
（CAN0=ECU@500k，CAN1=app_can_gw UDP4000），主机侧：

```bash
cd host/can_bridge
sudo ./vcan_setup.sh
./build_cannelloni.sh
./run_tunnel.sh &            # cannelloni -t 1000，避免默认 100 ms 延迟
candump vcan0                # 能看到真实总线
```

然后 `j1939_monitor.py -i vcan0` / `dm1_decode.py -i vcan0` 即可对真实板卡解码。
> 注：本方向的 DUT 固件（j1939_ecu）目前把 CAN1 用作板内测试仪；接 Bench B 需要把测试仪节点
> 去掉或改用 CAN2，已在第 31 章 U-32-2 登记为待确认项。

## 脚本

| 脚本 | 作用 |
|---|---|
| `run_selftest.sh` | 建 vcan0（若不存在）+ 跑 TP selftest |
| `run_monitor.sh` | 监视指定接口指定秒数 |
| `run_all.sh` | 依次跑 selftest / monitor / dm1 / 内核互操作，输出到 logs/ |
### 一键联调（--post，持锁内完成）

```bash
J1939_WITH_GW=1 bash scripts/build.sh samples/app/j1939_ecu
bash scripts/hwrun.sh samples/app/j1939_ecu --secs 14 --no-build \
     --post "timeout 70 bash host/j1939/run_benchb.sh" --lock-wait 1800
```

`run_benchb.sh` 内部：sudo vcan_setup → cannelloni 隧道 → cansend 教对端 → `j1939_monitor.py`
抓 8 s → `dm1_decode.py` 抓 3 s，日志写 `logs/<ts>-host-j1939-benchb.log`，
看到 EEC1 即判 PASS。

**真实输出（logs/20261030-181002-host-j1939-benchb.log，板端 GW 变体 @500k）**：

```
---- PGN 周期统计 ----
       PGN name                count  period avg(ms)   min(ms)   max(ms)
0x0F004 EEC1                  793          10.089     9.582    20.200
0x0FECA DM1                     7        1166.760   999.917  2000.241
0x0FEEE ET1                     7        1166.759   999.905  2000.262
0x0FEF1 CCVS1                  80         101.273    99.390   199.795
frames=887
[DM1] PGN=0x0FECA SA=0x00 lamp=0x10 [AWL] SPN=100/FMI=4/OC=1
BENCHB RESULT: PASS（vcan0 上收到板端 J1939 EEC1）
```

> 网关会让周期抖动变大（EEC1 max 20.2 ms、DM1 max 2000 ms），这是 cannelloni 攒包（-t 1000）
> 与 vcan 限速的结果，不是节点协议栈问题；节点侧抖动见第 31 章表 31-7 的 Bench A 数据。