# uds_flash —— UDS 34/36/37 下载 32 KB 镜像到片内 Flash

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 硬件与角色

CAN0 = ECU（0x7E0/0x7E8），CAN1 = 板内测试仪（反向）。无需外接线。

## 2. 下载区与镜像格式

- 下载区：`0x800F0000 ~ 0x800F8000`（1 MB 片内 Flash 的**最后 64 KB**，应用镜像之外的保留区）。
- 镜像 32 KB：`[0..7]` 魔数 `HPMIMG01`、`[8..11]` 长度 u32 LE、`[12..15]` 数据区 CRC32、
  `[16..255]` 保留 0xFF、`[256..]` 数据区（32512 B，xorshift 伪随机）。
- 镜像头占满 1 页（256 B），因此每次 0x36 的 256 B 写入都是**整页对齐**的 Page Program。

## 3. 命令

| 命令 | 作用 |
|---|---|
| `run` | 完整刷写序列：10 02 → 27 → 34 → 36×128 → 37 → 31 01 FF00 → 31 01 FF01 → 11 01 |
| `erase` | 擦除下载区（重跑前必须擦，否则启动自检会跳过） |
| `check` | 读回下载区魔数/长度/CRC 并重算 CRC |
| `fi` / `stat` | 配置 / 擦写统计 |

启动时若下载区已有有效镜像，打印 `PASS post-reset` 并跳过自测（避免"复位-刷写"死循环）。

## 4. 实测（2026-10-30，经典 500 kbit/s）

日志：`logs/20261030-180524-uds_flash.log`

| 阶段 | 实测 | 结论 |
|---|---|---|
| 34 请求下载（含擦除 32 KB） | 16137 µs，pending=1 | PASS |
| 擦除 32 KB | 14664 µs → 2.23 MB/s | 与第 21 章一致 |
| 36 传输 128 块 × 256 B | 1324486 µs → 24.2 KB/s（总线侧） | PASS |
| 每块 RTT | 10203 µs（37 帧 ISO-TP） | PASS |
| 37 退出传输 | `77` | PASS |
| 31 01 FF00 校验 | ECU=0x791B3AB8 = 本地 | PASS |
| 31 01 FF01 依赖检查 | 魔数/长度/CRC 全对 | PASS |
| XIP 直读 32768 B | memcmp identical | PASS |
| Flash 裸写 | 32768 B / 118941 µs = **269 KB/s** | PASS |
| 11 01 复位 | `51 01` → PPOR 复位 | PASS |

`[uds_flash] PASS pass=10 fail=0（23.7 KB/s，镜像 32768 B）`，总耗时 **1.35 s**。
复位后启动打印 `PASS post-reset: 0x800F0000 存在有效镜像 magic=HPMIMG01 len=32768 crc=0x791B3AB8`。

## 5. 易错点（两次真实的 FAIL 换来的）

1. **NOR Page Program 页内回绕**：最初从 0x800F0010 开始写 256 B，跨页部分回绕到 0x800F0000，
   把魔数写坏、CRC 对不上（当时代码里每块都跨页）。解决：镜像头占满一页 + 写入按页切分兜底。
2. **CRC 端序**：0x31 FF00 的例程结果最初用小端写，主机按大端解析，得到"字节反转"的 CRC
   （0xB83A1B79 vs 0x791B3AB8）。统一大端后一致。
3. **写完必须 invalidate L1 D-Cache**，否则 XIP 直读可能读到旧 cacheline。
4. **擦写期间关全局中断**：中断向量表在 Flash 里，XPI 忙时取指会拿到无效数据。
5. **不要在启动时自动刷写**：刷写末尾会 0x11 01 复位，自动跑就变成"复位-刷写"死循环
   （曾把板子带进 J-Link 都连不上的状态，需要多试几次烧写）。

## 6. 复现

`@bash
cd hpm5e31_1
bash scripts/hwrun.sh app/uds_flash --secs 25 --send "run" --lock-wait 1800
`@

期望最后一行：`[uds_flash] PASS pass=10 fail=0`；随后复位并打印 `PASS post-reset`。
