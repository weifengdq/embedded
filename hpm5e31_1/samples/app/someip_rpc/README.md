# someip_rpc —— SOME/IP 服务发现 + 方法 RPC

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

板端 SOME/IP 服务端：组播 **OfferService** + 方法 **REQUEST/RESPONSE**，配套上位机
`host/someip/someip_client.py`（自写）与 `host/someip/vsomeip_client/`（vsomeip 3.7.6）。

## 1. 接线

| 板端 | 主机 | 说明 |
|---|---|---|
| ENET（YT8011AN 1000BASE-T1） | `enp6s0` | 经千兆车载以太网转换盒直连 |
| 192.168.2.100/24 | 192.168.2.1/24 | 网关 192.168.2.1 |
| UDP 30490（SD） | 加入组播 224.244.224.245 | 服务发现 |
| UDP 30501（服务） | 任意端口 | 方法与事件 |

自检：`ping 192.168.2.100` 必须通；不通先看第 12 章 PHY 链路（`ip` 命令看 link）。

## 2. 服务定义

service 0x1234 / instance 0x0001 / major 0x01 / interface 0x01 / UDP 30501，事件组 0x0001。

| 方法/字段 | ID | 载荷 | 返回 |
|---|---|---|---|
| add | 0x0001 | u32 + u32 | u32 |
| get_version | 0x0002 | — | u32 长度 + "hpm5e31-someip/1.0.0" |
| set_led | 0x0003 | bool（1 B） | bool（4 B，低字节有效） |
| temperature getter | 0x0004 | — | f32（℃，初始 31.5） |
| temperature setter | 0x0005 | f32 | f32（回读值） |
| get_time | 0x0006 | — | u32 微秒时基（上位机 NTP 式对齐用） |
| event counter | 0x8001 | 100 ms | u32 计数 + u32 板端时刻(us) |
| event adc | 0x8002 | 10 ms | u32（ADC0.IN1/PF26，16 bit） |
| event temperature | 0x8004 | 500 ms | f32 |

## 3. 命令

```bash
cd hpm5e31_1
# 一条龙：编译 + 烧写 + 串口自证 + 持锁跑上位机（推荐）
bash scripts/hwrun.sh app/someip_rpc --secs 8 --send "selftest;run;stat" \
     --post "timeout 80 bash host/someip/run_client.sh core" --lock-wait 1800

# 板端 shell 命令
selftest    # 协议层 7 项自证（无上位机）
run         # 方法级回环自证 8 项（无上位机）
stat        # 收发/错误/订阅计数
ip / offer / stopoffer / sub
```

## 4. 实测（2026-10-30，详见 `docs/chapters/35-someip-vsomeip.md` 表 34-8/34-9）

- 板端 `selftest`：T1~T7 **7/7 PASS**（含 SD Offer 编码回读、4 KB TP 回环、未知方法/服务错误码）；
- 板端 `run`：**8/8 PASS**（add 正常/回绕、get_version、set_led 回读、温度 get/set、未知方法、REQUEST_NO_RETURN）；
- 主机 `core`：SD 发现 PASS（组播 Offer + FindService 响应）、RPC 全 PASS、事件订阅 PASS；
- 日志：`logs/20261030-172454-someip_rpc.log` 等（每次上板一条，含串口自证与上位机输出）。

## 5. 易错点

1. **上位机必须先加组播成员**（`IP_ADD_MEMBERSHIP 224.244.224.245 on 192.168.2.1`），否则收不到 Offer，只能靠 FindService 单播回包发现。
2. **`Length` 字段按规范填 8+载荷**，填成整包长度会被板端拒（`stat` 里 `malformed` 增加）。
3. 板端第一次 Offer 在链路 up 之后发出（启动时最多等 3 s），交换机/转换盒刚上电时上位机要重试。
4. 板端只有一个服务实例，两个上位机同时 RPC 时 Session 由板端回显、不校验唯一性（U-35-6）。
