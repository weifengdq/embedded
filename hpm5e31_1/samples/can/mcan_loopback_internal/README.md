# mcan_loopback_internal — 4 路 MCAN 内部环回自检

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 MCAN 内部环回模式（`CCCR.TEST=1` + `TEST.LBCK=1`）在不接任何外部节点、不驱动
CAN 收发器的情况下，自证 HPM5E31 的 4 个 MCAN 实例、消息 RAM 分配与位时序配置通路：

| 轮次 | 帧格式 | 标识符 | DLC | 说明 |
|---|---|---|---|---|
| round0 | 经典 CAN 2.0B | 标准 0x123 | 8 | 阻塞发送 + 从 RXFIFO0 读回 |
| round1 | 经典 CAN 2.0B | 扩展 0x12345678 | 8 | 验证 XTD |
| round2 | CAN FD (BRS) | 标准 0x1AB | 15 (64 B) | 验证 FDF/BRS + 64 字节负载 |
| round3 | CAN FD (BRS) | 扩展 0x1ABCDEF0 | 15 (64 B) | 扩展 ID + FD |

每个实例 4 轮，共 16 项，全部 PASS 时打印 `SUMMARY: pass=16 fail=0 ALL PASS`。

## 硬件要求 / 接线

- 只需 J-Link + 串口（/dev/ttyACM0, 921600）。
- **不需要** CAN 收发器/总线/终端电阻：内部环回时 TX 在片内回环到 RX。
- 状态灯可选：每轮点亮对应通道的 TX/RX 灯（PF26/PF18 等），仅作视觉指示。

## 编译与运行

    cd hpm5e31_1
    bash scripts/hwrun.sh can/mcan_loopback_internal --secs 8 --send "info;test;stat"

## 实测现象（串口输出）

完整日志 `logs/20261030-110650-mcan_loopback_internal.log`，关键片段：

    === mcan_loopback_internal: 4 x MCAN 内部环回 ===
    [CAN0]
      CAN0 round0 PASS id=0x123 dlc=8 len=8 lec=0 tec=0 rec=0
      ...
    SUMMARY: pass=16 fail=0 ALL PASS src_clk=80000000 Hz

4 个实例的 round0~round3 全部 PASS，LEC=0、TEC=0、REC=0；构建产物 AHB_SRAM 使用 10 KB / 32 KB（4 x 2560 B）。

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 打印实例/IRQ/时钟/消息 RAM 地址 |
| `test` | 重新跑 16 项自检 |
| `stat` | 打印 4 个实例的 PSR/ECR（活动态、LEC、TEC/REC、bus-off） |

## 关键点 / 易错点

1. **消息 RAM 必须放 AHB_SRAM**：HPM5E31 的 `MCAN_SOC_MSG_BUF_IN_AHB_RAM=1`，
   缓冲区必须用 `ATTR_PLACE_AT(".ahb_sram")` 放 0xF0200000 起 32 KB，并调用
   `mcan_set_msg_buf_attr()` 注册；否则 `mcan_init()` 返回 `status_mcan_ram_out_of_range`。
2. 内部环回的 TX 不经过引脚，所以即使 `init_mcan*_pins()` 没配也“能通”；本样例仍调用
   `board_can_init_pins()`，便于与双节点例程对照。
3. FD 轮次需要 `mcan_get_default_ram_config(..., true)` 重算 RAM 布局（64 B 元素），
   否则 8 字节元素装不下 64 B 帧。
4. `mcan_deinit()` 后重新 `mcan_init()` 才能切换经典/FD 格式；`mcan_deinit` 会停时钟。
