# qei_gpio_sim —— GPIO 软件模拟 A/B 正交 + QEI0 解码

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 GPIO 状态机产生 4 倍频正交序列（PD07=A、PD06=B），QEI0 在 PC07=A、PC06=B 上解码，
串口打印位置/角度/Z 计数与发生器边沿数。

## 硬件要求 / 接线（重要）

**QEI 的 A/B/Z 输入只接 pad 的 QEI 复用功能，而 GPIO 输出占用同一个 pad 的复用功能，
所以同一个 pad 不可能既输出又输入**——必须飞线：

| 飞线 | 从 | 到 |
|---|---|---|
| 1 | PD07（模拟 A 输出） | PC07（QEI0_A） |
| 2 | PD06（模拟 B 输出） | PC06（QEI0_B） |

没有飞线时位置计数恒为 0，这也是本示例的自证内容之一（"没信号就是 0"）。

## 编译与运行

```bash
bash scripts/build.sh qei/qei_gpio_sim
bash scripts/hwrun.sh qei/qei_gpio_sim --secs 8 --send "qrun 20000;qstat;qstop;qstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `qrun <edges_per_s>` | 设置模拟边沿速率并启动（例 `qrun 20000`） |
| `qstop` | 停止模拟并打印已产生的边沿数 |
| `qstat` | 打印 QEI 位置/角度/Z 计数与发生器状态 |

## 实测现象（串口输出）

无飞线（本远程环境）：`qstat` 显示 `phase=0`，同时 `sim edges` 持续增加，
并提示"检查 PD07->PC07 / PD06->PC06 两根飞线"。
有飞线时：`phase` 应随边沿递减/递增，`qrun 20000` 约对应每秒 20000 个 4 倍频计数。

## 关键点 / 易错点

1. **QEO→QEI 也不能内部自环**：TRGM 与 QEI 之间只有"触发/暂停"通路，
   QEO 的 ABZ 输出没有进 TRGM 的通道（`hpm_trgmmux_src.h:210-217`）。
2. QEI0 与 QEO0 的引脚组"同名"（PB/PC/PD/PF 各有 A/B/Z），但同组只能是同一个 pad，
   所以回环必须选**不同组**（QEO0 用 PF05/06/07，QEI0 用 PC05/06/07）。
3. 位置为 0 时先查飞线，再查 `qeiv2_config_mode()` 的 `phcnt_max` 是否配了。
