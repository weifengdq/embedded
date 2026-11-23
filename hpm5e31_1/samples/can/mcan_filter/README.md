# mcan_filter — MCAN 过滤器路由测试

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

MCAN0 内部环回 + 全局过滤配置（GFC）设为"未命中即拒收"，用探针 ID 验证 5 类过滤器
（经典/范围/双 ID/FIFO0 与 FIFO1 分流/标准+扩展）的匹配与丢弃行为。共 14 个探针。

## 硬件要求 / 接线

- 只需 J-Link + 串口；内部环回不驱动总线，**不需要**收发器/总线/终端电阻。

## 编译与运行

    bash scripts/hwrun.sh can/mcan_filter --secs 6 --send "info;test"

## 实测现象（串口输出）

完整日志 `logs/20261030-114549-mcan_filter.log`，结果 `SUMMARY: pass=14 fail=0 ALL PASS`：

| 场景 | 配置 | 探针 -> 实际路由 |
|---|---|---|
| 经典精确 | 0x123 / mask 0x7FF -> FIFO0 | 0x123 -> FIFO0；0x124 -> 丢弃 |
| 范围 | [0x200,0x20F] -> FIFO0 | 0x200/0x20F -> FIFO0；0x210 -> 丢弃 |
| 双 ID | 0x300 / 0x301 -> FIFO0 | 0x300/0x301 -> FIFO0；0x302 -> 丢弃 |
| FIFO 分流 | 0x400 -> FIFO0，0x401 -> FIFO1 | 0x400 -> FIFO0；0x401 -> FIFO1；0x402 -> 丢弃 |
| 标准+扩展 | 标准全通 -> FIFO0；扩展 [0x1ABCDE00,0x1ABCDEFF] -> FIFO1 | 0x010 -> FIFO0；0x1ABCDE12 -> FIFO1；0x1ABCDD00 -> 丢弃 |

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 打印场景说明 |
| `test` | 重跑 5 个场景 14 个探针 |

## 关键点 / 坑

1. **GFC 必须设成 reject**：驱动默认把未命中帧放进 RX FIFO0，不设
   `MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT` 的话"过滤器没生效"的假象会让人怀疑人生。
2. 过滤器元素的 `can_id_type`（0=标准/1=扩展）要与 `filter_type` 匹配，否则驱动校验失败。
3. 每次场景都重新 `mcan_init()` 以清空 FIFO，避免上一场景的残留帧干扰判定。
4. 探针判定同时读 FIFO0 与 FIFO1：若两处都有帧视为配置错误（代码里返回 4 并计 FAIL）。
