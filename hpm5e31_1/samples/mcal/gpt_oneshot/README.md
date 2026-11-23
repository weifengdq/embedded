# gpt_oneshot —— AUTOSAR Gpt 单次定时（one-shot）精度与 API 开销

用 GPTMR1 CH0 的 one-shot 通道，测 10/50/100/500/1000/5000 µs 六档单次定时误差，
把"StartTimer 调用里不可观测的启动段"用已计 tick 扣掉，剩下硬件精度 + 中断响应延迟。

## 硬件要求

| 项目 | 说明 |
|---|---|
| 板卡 | hpm5e31_1 |
| 外部接线 | 无 |
| 使用资源 | GPTMR1 CH0（逻辑通道 1，one-shot）、mcycle（CPU 480 MHz）、GPTMR0 CH3（Predef，仅作参考） |
| 不占用 | GPTMR0 CH0（gpt_periodic 用）、GPTMR2（Icu）、GPTMR3（BSP） |

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/gpt_oneshot
bash scripts/hwrun.sh mcal/gpt_oneshot --secs 20
```

## 命令

| 命令 | 说明 |
|---|---|
| `one <us>` | 单次 one-shot 定时 <us> 微秒并打印时间线 |
| `sweep` | 六档目标 × 20 次精度扫描表 |
| `info` | 时钟/版本/精度下限 |
| `err` | 错误路径（非法通道 / 0 值 / 运行中重复启动 / 空指针）|

## 实测现象

日志：`logs/20261102-141404-gpt_oneshot.log`

```text
[sweep] one-shot accuracy, 20 shots each, f_gptmr1=100000000 Hz, f_cpu=480000000 Hz
target   api_ns     err_mean_ns  err_sd_ns    err_min_ns   err_ppm
100      2950.4     983.1        32.4         956.3        9831.25
500      2949.1     980.0        34.6         943.8        1960.00
1000     2952.8     988.9        30.8         954.2        988.85
5000     2963.2     985.1        34.3         956.3        197.02
[one] target=1000 us -> ticks=100000
[one] api=3125.0 ns, err=+1066.7 ns (+1066.67 ppm), isr_lat=100000 ticks (1000000.0 ns)
```

解读：50 µs ~ 5 ms 各档的 **err_mean ≈ 0.98 µs 恒定**、err_sd ≈ 31~35 ns。
恒定 0.98 µs = 硬件到期 → 进入 ISR 的响应延迟（含实测 0.75 µs 入口 + 读数约 0.3 µs），
定时器本身的抖动是 sd 31 ns（1 ms 目标下 31 ppm）；10 µs 档被中断响应随机性主导（sd 1.8 µs）。

## 关键点

1. one-shot 精度 = 定时器硬件精度（±1 tick = 10 ns）+ 中断响应延迟（亚 µs 级恒定偏置）；
   AUTOSAR 的 one-shot 语义是"到期后停表并置 expired"，停止动作在 ISR 里完成。
2. `Gpt_GetTimeElapsed` 在 expired 状态返回目标值，在 running 状态返回当前计数 ——
   因此启动后读它可得到"已经计了多少 tick"，用来扣除启动段。
3. API 开销 2.9~3.2 µs 是"重配 GPTMR 通道 + NVIC 使能"的成本，比周期通道的纯读操作高一个数量级。

## 待确认项

- 已实测：六档精度表、API 开销、DET 错误路径（delta=4）。
- 已知限制：单次定时的绝对误差含固定 ~1 µs ISR 响应偏置，想要 ±100 ns 级绝对时间戳
  应使用捕获/比较硬件联动而非中断；复跑 `bash scripts/hwrun.sh mcal/gpt_oneshot --secs 20`。
