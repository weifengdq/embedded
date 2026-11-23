# someip_event —— SOME/IP 事件订阅（SubscribeEventgroup + NOTIFICATION）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

板端周期发布 3 路事件，上位机用 **SubscribeEventgroup** 订阅事件组，统计周期/抖动/丢包/单向时延。
组件与接线同 `someip_rpc`（详见 `components/app_someip/README.md`、第 34 章）。

## 1. 事件定义

service 0x1234 / instance 0x0001 / 事件组 0x0001 / UDP 30501（事件端点由订阅者 SD option 指定）

| 事件 | ID | 周期 | 载荷 |
|---|---|---|---|
| counter | 0x8001 | 100 ms | u32 计数 + u32 板端发送时刻（us，供单向时延估计） |
| adc | 0x8002 | 10 ms | u32（ADC16 ADC0.IN1 = PF26，16 bit 原始值；悬空=噪声底） |
| temperature | 0x8004 | 500 ms | f32（℃） |

订阅语义：板端收到 `SubscribeEventgroup` 后回 `SubscribeEventgroupAck`（TTL 3 s），把订阅者
IPv4 endpoint（来自 SD option）记入订阅表；**没有订阅者不发事件**；TTL 到期自动摘除。

## 2. 命令

```bash
cd hpm5e31_1
bash scripts/hwrun.sh app/someip_event --secs 6 --send "selftest;run;evstat" \
     --post "timeout 80 bash host/someip/run_client.sh event" --lock-wait 1800
# 板端 shell
run      # 板内模拟订阅者：灌 Subscribe -> 验 Ack/订阅表 -> 模拟 1 s 时间推进，统计事件条数
evstat   # 事件生成次数 vs 实际发出 NOTIFICATION 条数
stat / sub
```

## 3. 实测（2026-10-30）

板端 `run`（1 s 窗口，模拟时基）：counter **10 条**、adc **100 条**、temp **2 条** → PASS
（期望 = 1000/100+1、1000/10+1、1000/500+1）。日志 `logs/20261030-173653-someip_event.log`。

主机 `event --secs 6`：收到 **391 条 NOTIFICATION / 6.00 s，10.6 kbit/s**（订阅前 0 B）；
counter 100.01 ms（σ0.189，最大间隔 100.37，丢包 0）、adc 10.03 ms（σ0.535，max 19.88）、
temperature 499.94 ms（σ0.415）；counter 单向时延 **0.39 ms 均值 / 0.58 ms 最大**。
注意 6 s 窗口只覆盖约 3.5 s 事件，因为本客户端只订阅一次不续约（TTL 3 s + 500 ms 余量后板端摘除）——
这正是"必须每 TTL/2 续约"的实测证据。日志 `logs/20261030-174718-host-someip-event.log`。

## 4. 坑

1. **订阅成功 ≠ 收到事件**：Ack 送订阅者 SD 端口（30490），事件送 Subscribe option 里的端点（本章 30502）。
   更隐蔽的是 **Subscribe 条目第 4 字节的 num_options 必须写 1**，写 0 时板端认为"没有 endpoint option"，
   会退回把事件发到订阅者 SD 端口 → 主机"看到 Ack 但 0 条事件"（本章踩过，见第 34 章坑 5）。
2. **必须续约**：TTL=3 s，订阅者每 1.5 s 重发；不续约 3.5 s 后事件停。
3. **adc 是真实 ADC16 采样**（PF26 悬空=噪声底），不是合成波形；要稳定值需外接分压/电位器。
4. 事件端点端口不要用 30490/30501（本章用 30502）。
