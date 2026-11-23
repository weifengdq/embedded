# vss_signals —— COVESA VSS 信号映射（板端发送 + 主机解码）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

板端按 VSS 语义周期发 3 帧（0x400/0x401/0x402，100 ms），CAN1 跑 cannelloni 网关，
主机 vcan0 抓包后用 `host/vss/vss_decode.py` 解成 VSS 路径 + 物理值并做范围/路径校验。

## 接线

CAN0(PD00/PD01) = 信号源；CAN1(PD05/PD04) = 网关（UDP 4000 ↔ 主机 vcan0）；两通道板内同一总线。

## 命令

```bash
bash scripts/hwrun.sh app/vss_signals --secs 10 --send "vstat" \
     --post "timeout 45 bash host/vss/run_vss.sh" --lock-wait 1800
```

串口：`vss on|off`、`rate <ms>`、`vstat`。上电自动 `vss on`（100 ms）+ 网关模式。

## 实测（logs/20261030-175900-vss_signals.log）

```
=== VSS 解码：vcan0 (3.0 s)，共 90 帧 ===
命中 VSS 帧 ID：0x400, 0x401, 0x402
  Vehicle.Speed                                 0x400 km/h     31   64.80   82.80  100.80 OK
  Vehicle.Powertrain.CombustionEngine.Speed     0x400 rpm      31 2464.00 4024.00 5584.00 OK
  Vehicle.Powertrain.CombustionEngine.ECT       0x401 celsius  29   83.00   85.17   87.00 OK
  Vehicle.Powertrain.Transmission.CurrentGear   0x401 none     29    1.00    1.90    6.00 OK
  Vehicle.Cabin.Door.Row1.DriverSide.IsOpen     0x401 none     29    0.00    0.34    1.00 OK
  Vehicle.Powertrain.TractionBattery.StateOfCharge.Current 0x402 percent 30 66.40 72.20 78.00 OK
  信号覆盖 6/6，范围检查 PASS，VSS 路径校验 PASS → RESULT: PASS
```

## 坑

1. **网关 peer 必须显式配置**：纯发送场景下 app_can_gw 的 peer auto-learn 不会触发，
   固件里写死 `peer_ip=192.168.2.1, peer_port=4000`（第一版全 0 帧的根因）；
2. **i8 温度先补码再偏移**：raw 0x93 = 147 → 补码 -109？注意本项目用 `int8_t` 直接转换后加 40 再偏移，
   解码端按 `raw>127 ? raw-256 : raw` 处理；
3. **VSS 路径拼错不会报错**：用 `--vss-json` 校验叶子存在，否则数据看起来正常但上层匹配不到。
