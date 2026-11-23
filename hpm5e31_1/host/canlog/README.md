# host/canlog —— candump 记录解析与回放校验

配套 `samples/app/can_logger`（板端 RAM 环形缓冲 + candump 格式导出）。

## 1. 依赖

`$HOME/pyenv/bin/python`（标准库即可；`--png` 需要 matplotlib）。
回放侧还需要 can-utils（`candump`/`canplayer`/`cansend`，Ubuntu: `sudo apt install can-utils`）。

## 2. 板端导出 candump 文本

```bash
bash scripts/hwrun.sh app/can_logger --secs 12 \
     --send "stat;replay 20 300;log dump 20;verify" --lock-wait 1800
# 串口输出里 "# candump:" 到 "# end candump" 之间就是 candump 文本
```

## 3. 解析

```bash
sed -n '/# candump:/,/# end candump/p' logs/<时间戳>-can_logger.log > /tmp/candump.txt
$HOME/pyenv/bin/python host/canlog/candump_parse.py /tmp/candump.txt \
     --csv /tmp/frames.csv --png /tmp/seq.png --expected-seq 20
```

输出：帧数、时间跨度、平均帧率、总线负载估算、ID 直方图、帧间隔、**0x300 seq 顺序/丢帧校验**，
退出码 0 = PASS。

## 4. 回放链路（canplayer -> vcan0 -> 网关 -> 板端）

```bash
# 板端：can_gateway 固件（CAN1 网关 + CAN0 记录/回显）
bash scripts/hwrun.sh app/can_gateway --secs 20 --send "gw on;quiet" \
     --post "timeout 60 bash host/canlog/replay_check.sh" --lock-wait 1800
```

`replay_check.sh` 用 `canplayer -I <candump文件>` 把记录灌回 vcan0，板端网关搬到 CAN1，
CAN0 上的记录节点按序收到 0x300 帧后由 `verify` 判定。

> 状态：**待确认（需实测）**。板端 RAM 记录 + 串口 dump + MCU 内回放（CAN1 发、CAN0 收）已通过
> （见 `samples/app/can_logger/README.md`）；canplayer + 网关版本的回放链路依赖
> `host/can_bridge`（tp-infra）与 `can_gateway` 固件，尚未联调。

## 5. candump 文本格式

```
(1793000001.234567) can0 300#01000000A5A5A5A5
(1793000001.244101) can0 181#0102AABB
```

- 括号内是 epoch 秒.微秒；`can0` 是通道；`ID#DATA` 十六进制；扩展帧为 8 位 ID（如 `18FEF100`）。
- 板端 `log dump` 的时间戳基准是 2026-10-30 00:00:00（固定 epoch 1793000000）+
  `mcycle` 相对时间，仅用于**相对顺序/间隔**分析，不是墙钟时间。
