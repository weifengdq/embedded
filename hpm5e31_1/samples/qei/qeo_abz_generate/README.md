# qeo_abz_generate —— QEO 产生 ABZ 正交输出

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 QEO0 把"位置"还原成 ABZ 正交波形（模拟编码器）：

```text
QEI0 的位置计数器(COUNT[PH], 软件写入) -> TRGM 位置矩阵 -> QEO0 -> ABZ 输出
   PF07 = A, PF06 = B, PF05 = Z
主循环按 `qgen <pos_per_s> <lines>` 的设定递增位置, QEO 就按线数输出正交波形。
```

## 硬件要求 / 接线 / 观测方式

QEO 的 ABZ 是**输出引脚，无法在片内回读**（TRGM 与 QEO 之间只有 TRIG_IN / DAC_VALID，
没有 ABZ 反送通道）。因此观测方式只有三种：

1. **示波器/逻辑分析仪**（推荐）：探头接 PF07(A)、PF06(B)、PF05(Z)，可看相位差与 Z 宽度；
2. **3 根飞线闭环**：PF07→PC07、PF06→PC06、PF05→PC05，再用 QEI0 解码验证位置；
3. **无仪器时**：用本例程的寄存器自证——打印 QEI0 位置（QEO 的位置来源）与
   QEO0 `ABZ.RESOLUTION`（线数回读），确认"位置→QEO"的内部配置链路正确。

## 编译与运行

```bash
bash scripts/build.sh qei/qeo_abz_generate
bash scripts/hwrun.sh qei/qeo_abz_generate --secs 10 --lock-wait 900 \
  --send "qgen 2000 1000;qshow;qhalt"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `qgen <pos_per_s> <lines>` | 设定位置增量与线数并启动（例 `qgen 2000 1000`） |
| `qhalt` | 停止 |
| `qshow` | 打印 QEI 位置/角度、QEO ABZ 线数回读、引脚信息 |

## 实测现象（串口输出）

```text
=== qeo_abz_generate (board hpm5e31_1, ext 1.0.0) ===
QEO0 ABZ out: A=PF07 B=PF06 Z=PF05; 位置来源 = QEI0 位置寄存器(软件写)
QEI0 phase=0 (软件写, 即 QEO 的位置源)  pos=0  angle=0
  QEO0 ABZ.RESOLUTION=0x... (线数回读)  mode 已设为 qeo_abz_output_abz
  lines=1000 step=2000/s running=0  ABZ 引脚: A=PF07 B=PF06 Z=PF05
```

实测日志：`logs/QEO_LOG`。

## 关键点 / 易错点

1. **QEO 不能自环测自己**：ABZ 是纯输出，SoC 内没有回读通路（见第 16 章 §16.4.5）。
2. QEO 与 PWM 实例配对使用（`BOARD_QEO_PWM` 注释：QEO1 ↔ PWM1），
   做 PWM/DAC 输出模式时要确认同编号 PWM 的时钟与配置。
3. 位置来源必须经 **TRGM 位置矩阵**（`trgm_pos_matrix_config`），
   直接把 QEI 的位置寄存器读给 QEO 是不存在的通路。
4. HPM5E00 的 QEO 是 **QEOv2**（`QEOV2_Type`、`hpm_qeov2_drv.h`），
   `qeo_abz_mode_t` 只有 `output_type` 等少数字段，别照抄其它系列的
   `lines/phase_offset/enable_a...` 字段（会编译不过）。
5. 线数用 `qeo_abz_set_resolution_lines()` 设置，回读 `QEO0->ABZ.RESOLUTION` 验证。
