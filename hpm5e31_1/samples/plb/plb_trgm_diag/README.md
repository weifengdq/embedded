# plb_trgm_diag —— PLB / TRGM / GPTMR 捕获通路分段诊断（第 15、17 章）

把“PLB 输出经 TRGM 进 GPTMR 捕获计数恒为 0”拆成 5 段，每段单独观测：

| 段 | 观测点 | 手段 |
|---|---|---|
| ① 源 | GPTMR0 CH2/CH3 比较输出 | 读 `CR`（CEN/CMPEN）、`CNT`、`RLD`、`CMP0` |
| ② 路由 | TRGM 输出 | 读 `TRGM_OUT[0x38/0x39]` 回读位 |
| ③ PLB | TYPE A ch0 LUT0 输出 | 读 `TRGM_IN[0x44]`、`TYPE_A[0].LOOKUP_TABLE[0]` |
| ④ 捕获 | GPTMR0 CH2/CH3 `CAPPOS`/`CAPF` | 捕获中断计数 |
| ⑤ 引脚回环 | TRGM_P15 | PF15/PD15 + IOC `LOOP_BACK` |

## 命令

```text
dgen                        GPTMR0 CH2/CH3 各输出 1 kHz 方波（CMPEN=1）
dstat                       打印 TRGM/GPTMR/PLB 寄存器与各观测点 20 ms 翻转计数
ddirect <2|3>               OUTn --TRGM--> CAPT_n（跳过 PLB）
dplb <2|3>                  OUTn -> PLB_IN_00 -> LUT0(0xAAAA) --TRGM--> CAPT_n
dpin <2|3>                  PLB_OUT00 -> TRGM_P15 引脚 + LOOP_BACK -> CAPT_n（PF15/PD15 都试）
dsw <n> <us>                PLB 软件注入 n 个 us 宽脉冲，数捕获次数
dquad <ms>                  OUT2/OUT3 产生 90 度相差 A/B -> PLB TYPE B 4 倍频计数
dfilt <len> <mode> [hz]     引脚回环方波过 TRGM 数字滤波器，数捕获/测周期
dmon <min> <max>            GPTMR measure_width + MONITOR 阈值试验（单位 tick）
dall                        一次跑完整套（上板批测用）
```

## 2026-11-02 实测结论（HPM5E31 / hpm5e31_1）

- GPTMR0 CH2/CH3 寄存器层正常：`CR=0x00000508`（CEN+CMPEN）、CNT 递增、`RLD=99999`、`CMP0=49999`。
- 但 `TRGM_IN[]`/`TRGM_OUT[]` 回读**恒为 0**（连 PLB 软件注入都读不到），不能当片内逻辑分析仪用。
- `ddirect`/`dplb`/`dpin`/`dsw` 全部 0 次捕获；CAPT_2 与 CAPT_3 表现一致。
- 已知可用的捕获路径仍只有 `PWM0_TRGO_6 -> GPTMR0_CAPT_2`（见 `samples/pwm/pwm_basic`）。

日志：`logs/20261102-124850-plb_trgm_diag.log`（第 17 章 §17.9.5、第 15 章待确认 15-F/17-G）。
