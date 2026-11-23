# samples/app/xcp_can_cal —— XCP 在线标定 + Kp 扫参

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

第 33 章例程 2：在线改标定量（PID Kp/Ki、LED 周期、PWM 占空比），**写后立即生效**，
读回验证，用 MODIFY_BITS 改标志位，用 GET_SEED/UNLOCK 走一遍资源保护；最后做 Kp 0.1→2.0 扫参。

## 硬件接线

同 \`xcp_can_daq\`：CAN0 = XCP 从站（0x555/0x556），CAN1 = 板内最小主机，同一段总线，无需外部接线。
LED 用 PA02（用户灯），占空比/周期由标定量控制（1 ms 软件 PWM）。

## 标定量（A2L 绝对地址由 \`tools/gen_a2l.py\` 从 ELF 生成）

| 变量 | 类型 | 含义 |
|---|---|---|
| \`g_kp\` / \`g_ki\` | FLOAT32 | PID 比例/积分增益 |
| \`g_led_period_ms\` | UWORD | LED 呼吸周期（ms） |
| \`g_pwm_duty_pm\` | UWORD | LED PWM 占空比（千分比） |
| \`g_ctrl_enable\` | UBYTE | 控制环使能 |
| \`g_gain_axis[8]\` | UWORD[8] | 增益调度表 X 轴（A2L AXIS_PTS） |
| \`g_gain_curve[8]\` | UWORD[8] | 增益调度表 Y 值（A2L CURVE） |
| \`g_sw_flag\` | ULONG | MODIFY_BITS 演示用 |

观测：\`g_ctrl_y\`、\`g_ctrl_u\`、\`g_ctrl_y_pm\`、\`g_ctrl_max_pm\`、\`g_settle_ms\`、\`g_tick_cnt\`（1 kHz 控制环）。

## 命令

| 命令 | 作用 |
|---|---|
| \`info\` | 配置与标定量地址 |
| \`cal\` | 板内标定自证（读/写/回读/MODIFY_BITS/SEED/UNLOCK + Kp 扫参 10 点） |
| \`case\` | 打印当前标定值与最近一次阶跃响应 |
| \`step\` | 用当前增益重做一次阶跃响应 |
| \`clr\` | 清统计 |

上电自动跑 \`cal\`（约 15 s，10 个 Kp 点各 1.2 s 阶跃），串口最后一行 \`XCP CAL SELFTEST ...\`。

## 实测（2026-10-30）

日志：\`logs/20261030-173517-xcp_can_cal.log\`

| 项 | 结果 |
|---|---|
| SHORT_UPLOAD 读 g_kp | PASS |
| DOWNLOAD 写 g_kp=1.75 / 回读 | PASS（下一毫秒控制周期即生效） |
| DOWNLOAD 写 UWORD（占空比/周期） | PASS |
| MODIFY_BITS bit8..11 置 0xA | PASS（0xCAFEB0B0 → 0xCAFEBAB0） |
| 越界地址 DOWNLOAD | 被拒 ERR=0x23（不触发总线错误） |
| GET_SEED / 错 key UNLOCK / 正确 key | 4 B 种子 / ERR=0x25 / PASS |
| Kp 扫参 0.1→2.0（10 点） | 峰值 913→850‰（单调下降）；1.2 s 窗口未进 ±5% 带（见第 33 章 U-34-3） |

复现：

~~~bash
cd hpm5e31_1
bash scripts/hwrun.sh app/xcp_can_cal --secs 30 --send "case;clr" --lock-wait 1800
~~~

## A2L 与上位机

~~~bash
# 生成 + 校验（a2ltool 是 ref2 里的 Rust 工具）
python3 tools/gen_a2l.py --elf build/xcp_can_cal-debug/output/demo.elf \
        --vars tools/a2l/xcp_can_cal.json --out samples/app/xcp_can_cal/xcp_can_cal.a2l
a2ltool samples/app/xcp_can_cal/xcp_can_cal.a2l --check
# 上位机在线标定（Bench B，先 gw on）
python3 host/xcp/xcp_cal.py --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l --channel vcan0
~~~
