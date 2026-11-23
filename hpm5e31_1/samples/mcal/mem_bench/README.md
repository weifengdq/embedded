# mem_bench —— Mem 驱动擦除/编程/读取吞吐与作业延迟实测

## 功能

在保留窗口内（0x800F0000 起 16KB）用 AUTOSAR Mem 驱动做擦除、编程、读取吞吐测量，
并统计每个作业需要多少次 `Mem_MainFunction`、单次调用最长阻塞时间（决定能否放进 1ms 任务）。

## 硬件要求

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（片内 1MB XPI NOR） |
| 外部接线 | 无，只需 USR 串口 921600 |
| 擦写范围 | 0x800F0000 ~ 0x800F3FFF（4 个 4KB 扇区 = 16KB，保留窗口内） |
| Flash 寿命 | 每次运行擦 16KB、写 16KB；默认只跑 1 轮，请勿频繁大范围擦写 |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/mem_bench
bash scripts/hwrun.sh mcal/mem_bench --secs 30
```

## 命令

| 命令 | 说明 |
|---|---|
| membench | 依次测 16KB 擦除 / 16KB 编程 / 16KB 读取 / XIP memcpy 基线，打印吞吐与作业延迟 |

## 实测现象

日志：`logs/20261102-140321-mem_bench.log`

```text
--- mem bench: 16 KB @0x800f0000 (reserved window) ---
erase :   7.646 ms    2092.6 KB/s  4 sectors, mf_calls=4, max_call=1.908 ms
write :  61.136 ms     261.7 KB/s  64 pages,  mf_calls=64, max_call=0.978 ms
read  :   0.357 ms   44859.8 KB/s  32 chunks, mf_calls=32, max_call=0.020 ms
xip   :   0.340 ms   47064.6 KB/s  (memcpy baseline,不含 Mem 作业开销)
throughput summary: erase=2092.6 KB/s write=261.7 KB/s read=44859.8 KB/s
MEMBENCH: PASS
```

## 关键数据（第 47 章引用）

| 指标 | 数值 | 说明 |
|---|---|---|
| 扇区擦除 | 1.91 ms / 4KB ≈ **2.04 MB/s** | 4 个扇区 7.646 ms，每次 MainFunction 擦 1 个 |
| 页编程 | 0.955 ms / 256B ≈ **261.7 KB/s** | 64 页 61.136 ms，每次 MainFunction 编 1 页 |
| 读取（Mem_Read） | **44.9 MB/s** | 每 chunk 512B，32 次 MainFunction |
| 读取（XIP memcpy 基线） | 47.1 MB/s | 上限参考，差异来自作业状态机开销 |
| 单次 MainFunction 最长阻塞 | 擦 1.908 ms / 写 0.978 ms / 读 0.020 ms | 擦除是唯一超过 1ms 的操作 |

## 关键点

1. 擦除/编程时间由 NOR 器件决定（ROM API 阻塞），驱动只能决定"每次 MainFunction 做多少"；
   `MEM_UNITS_PER_MAIN_FUNCTION` 调大能减少调用次数、但会拉长单次阻塞。
2. 读取吞吐接近 XIP memcpy 上限，说明 Mem_Read 的分块拷贝开销可接受（约 4.7%）。
3. 写入 261.7 KB/s 是 ROM API 单页编程的真实水平；换 4KB 一次编程（`rom_xpi_nor_program` 支持
   一次多页）能更快，但那样单次 MainFunction 阻塞会到 ~15ms，不适合 1ms 任务；这是刻意的取舍。
4. Flash 寿命：擦除次数是最贵的资源。本驱动不提供自动磨损均衡（那是 Fee/NvM 的职责）。

## 两次运行一致性

| 指标 | 第 1 次（logs/20261102-140321） | 第 2 次（logs/20261102-141425） |
|---|---|---|
| 16KB 擦除 | 7.646 ms / 2092.6 KB/s | 7.650 ms / 2091.5 KB/s |
| 16KB 编程 | 61.136 ms / 261.7 KB/s | 61.164 ms / 261.6 KB/s |
| 16KB 读取 | 0.357 ms / 44859.8 KB/s | 0.357 ms / 44859.8 KB/s |
| 单次阻塞（擦/写/读） | 1.908 / 0.978 / 0.020 ms | 1.909 / 0.977 / 0.020 ms |

## 待确认项

- 无（上表两次运行偏差 <0.1%）。

## mcal_check 兼容项

`python3 tools/mcal_check.py` 对本模块与本例程的检查结果：

| 检查项 | 要求 | 本模块/例程情况 |
|---|---|---|
| C1 公共头干净 | include/*.h 与 cfg/*.h 不 include HPM SDK 头 | 同 mcal/Mem 模块，全部干净 |
| C2 驱动 .c 不硬编码实例/引脚 | 实例宏只在 PBcfg | Mem.c 只用 PBcfg 给的 XPI 基址与几何参数 |
| C3 五件套齐全 | 版本/API_ID/模块 ID/开关/guard | 见 mcal/Mem 目录 |
| C4 Det 模块 ID | MEM_MODULE_ID=91 | 一致 |
| C5 例程 README | hwrun 命令 + logs/ 真实日志 | 本文档含 hwrun 命令与 logs/20261102-140321-mem_bench.log |

复跑：`python3 tools/mcal_check.py`（当前对 mcal/Mem 与本例程 0 错 0 警）。
