# crc_bench —— AUTOSAR Crc 库：SWS 标准向量 + HW/SW 吞吐对比

## 功能

1. 8 个 CRC 算法对 SWS_Crc §7.2 的 "123456789" 校验值与 7 组标准数据向量自检；
2. 增量计算（`Crc_IsFirstCall=FALSE` 的 start value 链）一次算 vs 2/3 段算结果一致；
3. HPM CRC 外设（HW）与软件查表（SW）对 4KB 数据逐算法交叉校验；
4. 各算法 HW/SW 吞吐 MB/s、cycles/byte（mcycle 实测，CPU 480 MHz）。

## 硬件要求

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1，CRC 外设基址 0xF0080000（8 通道，poly 宽度 4~32） |
| 外部接线 | 无，只需 USR 串口 921600 |
| 资源 | CRC 外设整块归 Crc 模块（约定 §2），使用通道 0 |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/crc_bench
bash scripts/hwrun.sh mcal/crc_bench --secs 30 --send "crcinfo;crcvec;crcxcheck;crcbench"
```

## 命令

| 命令 | 说明 |
|---|---|
| crcinfo | 版本 + 每个算法走 HW 还是 SW，以及自校准选中的寄存器约定 |
| crcvec | SWS 标准校验值 + 7 组数据向量 + 链式（2/3 段）测试 |
| crcxcheck | 4KB 数据 HW 与 SW 结果比对（8 个算法） |
| crcbench | HW/SW 吞吐对比（4096B × 64 次） |
| crcraw | 诊断：外设原始探测（preset/自定义/pattern/CRC8），排查寄存器约定用 |

## 实测现象

日志：`logs/20261102-135730-crc_bench.log`

```text
Crc 1.0.0 module=201 vendor=0xffff
  CRC8 SAE J1850         HW accel  conv=0x009 (init_direct xor_hw feed_word poly_right, chain=no(SW))
  CRC8 H2F               HW accel  conv=0x009 (init_direct xor_hw feed_word poly_right, chain=no(SW))
  CRC16 CCITT-FALSE      HW accel  conv=0x109 (init_direct xor_hw feed_word poly_right, chain=yes)
  CRC16 ARC(0x8005)      HW accel  conv=0x109 (init_direct xor_hw feed_word poly_right, chain=yes)
  CRC32 Ethernet         HW accel  conv=0x009 (init_direct xor_hw feed_word poly_right, chain=no(SW))
  CRC32 P4(0xF4ACFB13)   HW accel  conv=0x009 (init_direct xor_hw feed_word poly_right, chain=no(SW))
  CRC32 P76(0x6938392D)  HW accel  conv=0x109 (init_direct xor_hw feed_word poly_right, chain=yes)
  CRC64 ECMA-182         SW table (no HW usable)

--- crc vector: check value of "123456789" (SWS_Crc 7.2) ---
[PASS] CRC8 SAE J1850         0x4B
[PASS] CRC8 H2F               0xDF
[PASS] CRC16 CCITT-FALSE      0x29B1
[PASS] CRC16 ARC(0x8005)      0xBB3D
[PASS] CRC32 Ethernet         0xCBF43926
[PASS] CRC32 P4(0xF4ACFB13)   0x1697D06A
[PASS] CRC32 P76(0x6938392D)  0x4DB36B68
[PASS] CRC64 ECMA-182         0x995DC9BBDF1939FA
...
crcvec: PASS
crcxcheck: PASS
--- crc throughput: 4096 B x 64 iters, cpu=480000000 Hz ---
algo                    sw_MBps  sw_cyc/B  hw_MBps  hw_cyc/B  speedup
CRC8 SAE J1850            22.81     21.04    60.20      7.97     2.64x
CRC8 H2F                  22.81     21.04    60.20      7.97     2.64x
CRC16 CCITT-FALSE         17.11     28.05    60.20      7.97     3.52x
CRC16 ARC(0x8005)         19.16     25.05    60.20      7.97     3.14x
CRC32 Ethernet            20.83     23.04    60.21      7.97     2.89x
CRC32 P4(0xF4ACFB13)      20.83     23.04    60.21      7.97     2.89x
CRC32 P76(0x6938392D)     19.96     24.04    60.21      7.97     3.02x
CRC64 ECMA-182            17.75     27.04    17.75      27.04     1.00x
```

## 关键数据

| 指标 | 数值 |
|---|---|
| SW 查表吞吐 | 17.1 ~ 22.8 MB/s（17.7 ~ 28.1 cyc/B） |
| HW 吞吐（≤32 bit 算法） | 60.2 MB/s（7.97 cyc/B，MMIO 写受限） |
| HW 加速比 | 2.64x ~ 3.52x |
| CRC64 | 只能 SW，17.75 MB/s（外设 poly 宽度上限 32） |
| HW/SW 一致性 | 8/8 算法在 4KB 数据上完全一致 |
| 链式语义 | 8/8 算法一次算 == 2 段/3 段算 |

## 关键点

1. **CRC 全走软件表**也没错，但外设能到 3 倍。外设的寄存器约定（init 是否反序、xorout 由硬件
   还是软件加、喂数按字节还是按字、poly 是否左对齐）没有文档，本驱动首次使用时用 16 组候选
   逐组和软件表比对自动标定，标不上就永久回落软件（不会输出错的 CRC）。
2. **CRC64 只能软件**：外设 poly 宽度最大 32 位。
3. **链式（分段）在部分算法上不能用 HW**：实测 8bit 与反射 32bit 算法的续算路径硬件结果与
   软件表不符（整段一致、分段不一致），驱动对这两类自动用软件续算，结果仍正确；
   `crcinfo` 的 `chain=no(SW)` 就是这条信息。第 47 章把它列为"已定位"限制。
4. **SWS 合规**：Crc §7.8 明确 "There are no development errors"，且 [SWS_Crc_00072] 禁止
   CRC 库函数调用 BSW（含 Det），因此本模块的 Calculate API 不做 Det 上报——这与"每个 API 都有
   Dev Error 检查"的一般要求不同，是 SWS 的明确例外，已在第 47 章说明。
5. 吞吐用 `mcycle` 测 CPU 周期，包含函数调用与状态机开销；总线受限（每次 32 位 MMIO 写约
   8 cyc/4B）是 HW 路径的瓶颈，不是 CRC 计算本身。

## 待确认项

- 8bit/反射 32bit 算法的硬件链式约定：已定位（整段一致、续算不一致），复跑命令
  `bash scripts/hwrun.sh mcal/crc_bench --secs 20 --send "crcinfo;crcraw"`。

## mcal_check 兼容项

`python3 tools/mcal_check.py` 对本模块与本例程的检查结果：

| 检查项 | 要求 | 本模块/例程情况 |
|---|---|---|
| C1 公共头不得 include HPM SDK 头 | include/*.h 与 cfg/*.h 干净 | Crc.h 只 include Std_Types/Mcal_Cfg/Mcal_ModuleId/Mcal_Version；Crc_Cfg.h、Crc_PBcfg.h 干净 |
| C2 驱动 .c 不得硬编码实例 | 实例宏只在 *_PBcfg.c | Crc.c 无 HPM_CRC、无引脚；CRC 基址、通道号、clock_crc0 全部来自 Crc_PBcfg.c |
| C3 五件套与版本/API_ID/模块 ID/开关/guard | 齐全 | include/Crc.h、src/Crc.c、cfg/Crc_Cfg.h、cfg/Crc_PBcfg.{h,c}；含 CRC_API_ID_*（0x01~0x09）、CRC_MODULE_ID=201 |
| C4 模块 ID 与 Det | 201 | Crc 按 SWS_Crc §7.6.1 无开发错误、且 [SWS_Crc_00072] 禁止调用 BSW，故不调用 Det（不是漏检，是 SWS 要求，见第 47 章） |
| C5 例程 README | hwrun 命令 + logs/ 真实日志 | 本文档含 hwrun 命令与 logs/20261102-135730-crc_bench.log |

复跑：`python3 tools/mcal_check.py`（当前对 mcal/Crc 与本例程 0 错 0 警）。
