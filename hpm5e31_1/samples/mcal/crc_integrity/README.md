# crc_integrity —— 用 CRC 校验一段 Flash 数据：通过 / 失败自证

## 功能

AUTOSAR 里 Crc 的典型用途是数据完整性校验。本例程把 Mem 与 Crc 组合起来：

1. `crcimage`：对正在运行的应用镜像 [0x80000000, etext) 算 CRC32/CRC16/CRC8，HW 与 SW 必须一致；
2. `crcflash`：通过 Mem 驱动把已知 pattern 写进保留扇区，读回算 CRC32/CRC16，与离线黄金值比对 → PASS；
3. `crctamper`：把同一扇区重写成"翻 1 bit"的数据，CRC 必须变化 → TAMPER DETECTED；
4. `crcblank`：擦除后整扇区 0xFF 的 CRC32 应与黄金值一致。

## 硬件要求

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（片内 1MB XPI NOR + CRC 外设） |
| 外部接线 | 无，只需 USR 串口 921600 |
| 擦写范围 | 0x800F0000 ~ 0x800F0FFF（1 个 4KB 扇区，保留窗口内） |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/crc_integrity
bash scripts/hwrun.sh mcal/crc_integrity --secs 30 --send "crcimage;crcflash;crctamper;crcblank"
```

## 命令

| 命令 | 说明 |
|---|---|
| crcimage | 应用镜像 CRC32/16/8，HW vs SW（只读，安全） |
| crcflash | 写 pattern → 读回 → CRC 与黄金值比对 |
| crctamper | 翻 1 bit 重写 → CRC 必须变化（篡改自证） |
| crcblank | 擦除态 4096×0xFF 的 CRC32 比对 |

## 实测现象

日志：`logs/20261102-140431-crc_integrity.log`

```text
--- image: CRC over app image 0x80000000..0x80016718 (91928 B) ---
image crc32 sw=0x3b03af1b hw=0x3b03af1b MATCH
image crc16 sw=0x5e9c hw=0x5e9c MATCH
image crc8  sw=0xfc hw=0xfc MATCH
image hw_active: crc32=1 crc16=1 crc8=1
--- flash: write pattern to 0x800f0000, read back, CRC ---
flash crc32=0x489b7fd2 golden=0x489b7fd2 MATCH
flash crc16=0x9ab8 golden=0x9ab8 MATCH
crcint flash: PASS
--- tamper: rewrite sector with 1 bit flipped (byte 100) ---
tamper crc32=0x1cd5ce95 golden=0x489b7fd2 expected_corrupt=0x1cd5ce95
crcint tamper: TAMPER DETECTED (crc32 != golden)
--- blank: erase sector, CRC over 4096 x 0xFF ---
blank crc32=0xf154670a golden=0xf154670a MATCH
crcint blank: PASS
```

## 关键数据

| 项 | 数值 |
|---|---|
| 应用镜像大小 | 91928 B（etext = 0x80016718） |
| 镜像 CRC32 | 0x3B03AF1B（HW = SW） |
| pattern CRC32 / CRC16 | 0x489B7FD2 / 0x9AB8（黄金值，离线算出） |
| 翻 1 bit 后 CRC32 | 0x1CD5CE95（≠ 黄金值，检测成功） |
| 擦除态 CRC32 | 0xF154670A |
| 单 bit 篡改检出 | 是（CRC32 汉明距离在此长度下可检出所有单 bit 错） |

## 关键点

1. **黄金值与实现无关**：0x489B7FD2 等常量是用独立的离线脚本按 CRC-32/ISO-HDLC 参数算出来的，
   不是"先跑驱动再抄结果"，所以这个比对真正证明驱动实现正确。
2. **Mem + Crc 组合就是 NvM/Fee 的最小闭环**：写→读回→CRC 比对，再叠加"擦除态校验"和
   "篡改检测"两条边界，覆盖了存储完整性的常见故障模式。
3. 应用镜像 CRC 用 `Crc_IsFirstCall=TRUE` 一次算完（91KB），HW 路径只需 ~1.5ms 量级；
   这也是 Bootloader 做镜像校验的典型用法。
4. 注意：擦写只用 1 个保留扇区，跑一次擦 3 次（flash/tamper/blank），请在寿命预算内使用。

## 待确认项

- 无（失败路径已实测：0x1CD5CE95 ≠ 0x489B7FD2，检测生效）。

## mcal_check 兼容项

`python3 tools/mcal_check.py` 对 Mem/Crc 两个模块与本例程的检查结果：

| 检查项 | 要求 | 本模块/例程情况 |
|---|---|---|
| C1 公共头干净 | include/*.h 与 cfg/*.h 不 include HPM SDK 头 | Mem.h、Crc.h 及两者 cfg/*.h 全部干净 |
| C2 驱动 .c 不硬编码实例/引脚 | 实例宏只在 PBcfg | Mem.c/Crc.c 无 HPM_XPI0/HPM_CRC/引脚宏，全部走 PBcfg |
| C3 五件套齐全 | 版本/API_ID/模块 ID/开关/guard | mcal/Mem 与 mcal/Crc 各 5 个文件 |
| C4 模块 ID 与 Det | 91 / 201 | Mem 用 MEM_MODULE_ID 上报；Crc 按 SWS 不调用 Det |
| C5 例程 README | hwrun 命令 + logs/ 真实日志 | 本文档含 hwrun 命令与 logs/20261102-140431-crc_integrity.log |

复跑：`python3 tools/mcal_check.py`（当前对 mcal/Mem、mcal/Crc 与本例程 0 错 0 警）。
