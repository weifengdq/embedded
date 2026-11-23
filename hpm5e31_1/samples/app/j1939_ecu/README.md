# j1939_ecu —— 发动机 ECU 模拟 + 板内测试仪

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 跑一台"最小发动机 ECU"（SA=0x00），CAN1 跑诊断测试仪（SA=0xF9），
同一 MCU 内两个 J1939 节点在 250 kbit/s 总线上互通，无需上位机（Bench A）。

## 硬件连接

| 信号 | 引脚 | 说明 |
|---|---|---|
| CAN0 | PD00/PD01 | 经 TCAN1044AV 上总线，与 CAN1 同一段，120 欧终端板上已接 |
| CAN1 | PD05/PD04 | 测试仪节点 |
| 调试串口 | /dev/ttyACM0 | 921600 8N1（hwrun.sh 自动抓） |

无需外部 CAN 分析仪；TCAN1044AV 的 STB 由 PE03 控制，`j1939_bus_init()` 前先调
`board_can_transceivers_enable()`。

## 命令

```bash
cd hpm5e31_1
bash scripts/hwrun.sh app/j1939_ecu --secs 14 --lock-wait 1800
# 上电自动跑一遍；串口 shell 可重跑：
#   run | stat | fault add <spn> <fmi> [oc] | fault clear | fault list | dm1 | req <pgn_hex> | mon <ms> | info
```

## 上电自测内容（9 项断言）

1. 地址声明：SA=0xFE 请求 0xEA00 → 等 250 ms → 广播 NAME(0xEE00)，测试仪校验 PGN/SA/NAME；
2. 周期 PGN：EEC1 10 ms / CCVS1 100 ms / ET1 1 s / DM1 1 s，统计到达间隔；
3. 请求/应答：测试仪请求 ET1 与 LFE（LFE 默认不周期发送，只能由请求触发），测端到端时延；
4. DM1 故障注入：SPN=100/FMI=4、SPN=190/FMI=1，校验灯字节 0x11（MIL+AWL）与 DTC；
5. DM2 历史：请求 0xFECB，10 字节走 RTS/CTS 传回 2 个 DTC；
6. DM11 清除：请求 0xFED3，DM1 灯熄灭、DTC 清空。

## 实测（logs/20261030-170249-j1939_ecu.log）

```
[PASS] addr claim: SA=0x00 NAME=0009180100000000 seen_at=251167 us
[PASS] periodic EEC1(10ms): n=150 gap min/avg/max = 8505/10000/11492 us
[PASS] request/response: 4/4 answered, latency min/avg/max = 955/972/988 us
[PASS] DM1 fault: lamp=0x11 MIL=1 RSL=0 AWL=1 PL=0 SPN=100 FMI=4 OC=1
[PASS] DM2 history via RTS/CTS: frames=1 dtc=2 (SPN 100/190)
[PASS] DM11 clear: lamp=0x00 dtc=0
=== RESULT: PASS (9 PASS / 0 FAIL) ===
bus0: tx=582 rx=9 err=0 | bus1: tx=9 rx=582 err=0
```

## 坑

- PDU2 报文没有 DA，"请求 ET1 的应答"仍是广播，不能用 DA 判断应答；本测试仪用"请求后 80 ms 内
  到达同 PGN"判定；
- 地址声明完成前不能发周期报文（本实现门控调度器），否则 EEC1 会先于 NAME 出现；
- CAN0/CAN1 都必须在 `board_can_init_pins()` + `board_can_transceivers_enable()` 之后初始化。
## Bench B（可选）：接主机 vcan0 + cannelloni 网关

默认构建是 Bench A（CAN1=板内测试仪）。要让主机通过网关访问 ECU：

```bash
# 1) 用 GW 变体构建（CAN1 让给 app_can_gw，ECU 在 CAN0；app_can 只支持 500k，故 GW 模式用 500k）
J1939_WITH_GW=1 bash scripts/build.sh samples/app/j1939_ecu
# 2) 烧写并持锁跑主机侧工具（vcan0 + cannelloni 隧道由脚本自己起）
bash scripts/hwrun.sh samples/app/j1939_ecu --secs 14 --no-build \
     --post "timeout 70 bash host/j1939/run_benchb.sh" --lock-wait 1800
```

GW 变体资源：Flash 146944 B / DLM 95168 B（72.6%），与 app_lwip+app_can_gw 叠加后仍有余量。
串口会打印 `[pass] j1939 gw ready: claim_sa=0x00 ...`。

**实测（logs/20261030-181002-host-j1939-benchb.log）**：主机 vcan0 8 s 收到 887 帧——
EEC1 793 帧（平均 10.089 ms，min 9.582 / max 20.2 ms）、CCVS1 80 帧（101.3 ms）、DM1/ET1 各 7 帧；
`dm1_decode.py` 正确解出 lamp=0x10(AWL)/SPN=100/FMI=4/OC=1；BENCHB RESULT: PASS。
注意：app_can 只提供 500k/FD 档，GW 变体用 500 kbit/s（标准 250k 见 Bench A）。

> 注意：GW 变体构建会覆盖 `build/j1939_ecu-debug` 的 CMake 缓存；
> 切回 Bench A 前先 `rm -rf build/j1939_ecu-debug` 再重新 build。

## 重跑语义（重要）

- 上电自动跑一次；自测运行期间收到 `run` 会打印 `[busy] ... ignored` 并忽略（避免嵌套重置统计）。
- 地址声明只在首次上电做一次；再次 `run` 会补发一次广播 0xEE00，测试仪才能重新看到 NAME。
- 因此冒烟特征串不要给本例如程加 `--send run`，用上电自动跑的结论行即可。