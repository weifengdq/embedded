# someip_tp —— SOME/IP-TP 大包分段与重组（4 KB）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

UDP 单帧 MTU 1500 → SOME/IP 载荷上限约 1400 B，超过要用 **SOME/IP-TP**：
Message Type 置 0x20，16 B 头后加 4 B TP 头（Offset(28b)/Reserved(3b)/More(1b)），每段数据 ≤1392 B（87×16）。

## 1. 双向用例

| 方向 | 方法 | 请求 | 响应 | 验证点 |
|---|---|---|---|---|
| 板 → 主机 | 0x0010 get_bulk | u32 len（1..4096） | len 字节图案（`(i*31+7)&0xFF`） | 主机按 offset 重组，校验图案 + CRC32 |
| 主机 → 板 | 0x0007 crc32 | 任意长度字节（≤4096） | u32 CRC32（IEEE 802.3，与 `zlib.crc32` 一致） | 板端重组后算 CRC，与主机本地值比对 |

## 2. 命令

```bash
cd hpm5e31_1
bash scripts/hwrun.sh app/someip_tp --secs 5 --send "selftest;run;tpstat" \
     --post "timeout 80 bash host/someip/run_client.sh tp" --lock-wait 1800
# 板端 shell
run      # 板内回环：4096 B -> 3 段；重组 CRC 一致；乱序段被拒绝
tpstat   # 段 rx/tx、完整报文、丢弃、重组器状态
```

## 3. 实测

板端 `run`：**分段 4096 B → 3 段 PASS、重组 CRC32 0x5D1C4EE3 一致 PASS、乱序段拒绝 PASS**
（日志 `logs/20261030-173700-someip_tp.log`）。
板端 `selftest` T4 同样用 4 KB 回环验证分段/重组（`tp 3 segs -> 4096 bytes, crc 0x5D1C4EE3 verified`）。
主机侧 `get_bulk(4096)` 的重组目前在真实链路上只收到末段（U-35-10，见第 34 章"待确认项与风险"）：
板端确实按 3 段发出（抓包可见 MT=0xA0、TP 头 offset=2784/more=0、1332 B UDP 载荷），
主机侧 `_collect_tp()` 的接收/过滤链仍待修。

## 4. 约束与坑

1. **非末段长度必须是 16 的倍数**（1392 = 87×16），否则 offset 换算错误；
2. **按序重组**：本实现只接受连续 offset，乱序/丢段整包丢弃并计 `tp_drop`（测试用例专门验证）；
3. 所有段头字段必须一致（Service/Method/Client/Session），不一致判为不同报文丢弃；
4. 逻辑报文上限 4 KB（`SOMEIP_MAX_PAYLOAD`），重组缓冲静态分配在 `.bss`；
5. 段长 1392 → UDP 载荷 1412 B，不触发 IP 分片；若超过 1450 B 会分片，而 SDK lwIP 的
   `IP_REASSEMBLY=0`（收端不重组）→ 直接丢包（第 34 章坑 9）。
