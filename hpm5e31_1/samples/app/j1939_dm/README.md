# j1939_dm —— J1939-73 诊断报文（DM1/DM2/DM3/DM11）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 = 发动机 ECU（SA=0x00，DM1 1 s 周期），CAN1 = 诊断测试仪（SA=0xF9）。
演示"故障出现 → 灯亮 → 清除 → 历史保留 → 清历史"的完整诊断生命周期。

## 命令

```bash
bash scripts/hwrun.sh app/j1939_dm --secs 20 --lock-wait 1800
# shell: run | dm add <spn> <fmi> [oc] | dm clear | dm hist | dm timeline | dm list | stat | info
```

## 上电自测（7 项断言）

1. 基线：无故障，DM1 lamp=0x00、DTC=0；
2. 注入 SPN=100/FMI=4 → 下一周期 lamp=0x10（AWL）；
3. 再注入 SPN=190/FMI=1 → lamp=0x11（MIL+AWL）；
4. DM11 清除激活 → lamp=0x00、DTC=0；
5. 请求 DM2 → 2 个历史 DTC（10 字节，走 RTS/CTS）；
6. DM3 清除历史 → 再请求 DM2 为空；
7. 灯状态时间线 >=4 条。

## 实测（logs/20261030-172059-j1939_dm.log）

```
[PASS] fault#1 SPN100/FMI4 -> lamp=0x10 AWL=1 SPN=100 FMI=4
[PASS] fault#2 SPN190/FMI1 -> lamp=0x11 MIL=1 AWL=1
[PASS] DM11 clear active: lamp=0x00 dtc=0
[PASS] DM2 history: frames=1 dtc=2 (SPN 100/190 via RTS/CTS)
[PASS] DM3 clear history: DM2 dtc=0 lamp=0x00

| idx | t(ms) | lamp | MIL | RSL | AWL | PL | DTC | SPN | FMI |
|   0 |   249 | 0x00 |  0  |  0  |  0  | 0  |  0  |   0 |  0  |
|   1 |  3249 | 0x10 |  0  |  0  |  1  | 0  |  1  | 100 |  4  |
|   2 |  4249 | 0x11 |  1  |  0  |  1  | 0  |  1  | 100 |  4  |
|   3 |  5249 | 0x00 |  0  |  0  |  0  | 0  |  0  |   0 |  0  |
=== RESULT: PASS (7 PASS / 0 FAIL) ===
```

## 易错点

- 无故障时 DM1 只有 2 字节（灯 + 闪灯）；解析器不要把 0xFF 填充当 DTC；
- DM1 单帧只装 1 个 DTC，多故障要按严重度上报或改用 DM2 多包；
- DM11 只清"激活"故障，历史仍保留；要清历史必须再发 DM3；
- 灯映射（FMI→MIL/AWL/PL）是演示策略，量产必须按 OEM 诊断规范替换。
