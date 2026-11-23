# tsns_temp —— 片内温度传感器 TSNS 读数与统计

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- 使能 TSNS 连续模式，1 s 打印一次温度（原始码值 + 摄氏度）；
- 命令 `temp` 单次读数、`tempstat n` 做 n 次统计（min/max/avg/pp）、`tempsr` 打印配置与 OTP 校准字；
- 验证"出厂校准是否已烧"：读 OTP shadow 21 的 base/en 位。

## 硬件要求 / 接线

无外部接线。TSNS 是片内温度传感器（RM §62 p.1235），与 ADC16 的内部温度通道无关
（HPM5E31 的 ADC16 没有温度通道）。

## 编译与运行

```bash
bash scripts/build.sh power/tsns_temp
bash scripts/hwrun.sh power/tsns_temp --secs 12 --send "tempsr;temp;tempstat 20"
```

## 命令

| 命令 | 说明 |
|---|---|
| temp | 读一次：raw（16 位）+ ℃ + age |
| tempstat [n] | n 次 ×100 ms 统计 min/max/avg/pp |
| tempsr | TSNS 缩放系数、OTP 校准字、max/min 记录 |

## 实测现象（串口输出）

日志：`logs/20261030-112302-tsns_temp.log`

```text
TSNS @0xf0090000
  scale      : 1/256 C per LSB (TSNS_TEMP_SCALE=256)
  continuous : enabled, validity bits set by HW
  OTP shadow21(TSNS cal): 0x03a008c3  base=0x8c3  en=1
  max raw=0x00002599  min raw=0x00002599

letter:/$ temp
TSNS raw=0x00002599 (9625) -> 37.60 C, age=17318323

letter:/$ tempstat 20
tempstat: n=20 (2.0 s) min=37.00 C max=38.00 C avg=37.85 C pp=1.00 C

t= 2011 ms  temp= 38.89 C  raw=0x26e3
```

要点：

- 温度 = raw / 256（TSNS_TEMP_SCALE=256）；
- OTP shadow21 = 0x03a008c3 → base=0x8c3、en=1，说明**出厂已校准**，不需要软件标定；
- 空载 2 s 内峰峰值 1 ℃，属于环境/自热波动。

## 关键点 / 坑

1. 必须先 `clock_add_to_group(clock_tsns, 0)` 再使能 TSNS，否则读到 0。
2. `tsns_get_current_temp_in_raw()` 内部会等 valid 位，不要在中断里调用（可能长时间自旋）。
3. 芯片跑重负载测试时自热会抬高 5~10 ℃，测环境温度请空载。
4. 若 OTP base=0 且 en=0，说明校准字未烧，需按 SDK `samples/drivers/tsns` 的流程做软件标定。

## 待确认项

| 项 | 状态 | 验证方法 |
|---|---|---|
| 与外部温度计的一致性 | 待确认（无恒温设备） | 放恒温箱/与板载温度计比对，做两点校准 |
| 过温阈值中断/复位 | 待确认（未做例程） | 配 `tsns_config_reset_threshold()` + 热风枪加热验证 |
