# spi_async_job —— AUTOSAR MCAL Spi 异步 Job 序列例程

验证 MCAL Spi 的异步路径：`Spi_AsyncTransmit` 立即返回，序列由 `Spi_MainFunction_Handling`
推进，4 个 Job 的序列（8 bit 一对 + 16 bit 一对）全部完成，统计 JobEndNotification /
SeqEndNotification 回调次数，并对比同步与异步轮询的 CPU 周期消耗。

## 硬件要求与接线

同 `spi_master_slave`：SPI1 主（PC10/11/12/13）↔ SPI3 从（PA26/27/28/29）杜邦线直连。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/spi_async_job
bash scripts/hwrun.sh mcal/spi_async_job --secs 25 \
  --send 'spi_job multi 64 32;spi_job cpu 64;spi_job hint 64;spi_job det'
```

| 命令 | 说明 |
|---|---|
| `spi_job info` | 版本、异步模式、状态、回调计数 |
| `spi_job multi [n8] [n16]` | 4 Job 异步序列（8 bit 一对 + 16 bit 一对），校验数据与回调次数 |
| `spi_job cpu [n]` | 同步 vs 异步轮询的内核周期对比 |
| `spi_job hint [n]` | 尝试中断模式（SetAsyncMode(SPI_ASYNC_MODE_INTERRUPT)） |
| `spi_job det` | 非法参数 Det 计数 |

## 实测现象与数据

串口日志：`logs/20261102-142516-spi_async_job.log`、`logs/20261102-142810-spi_async_job.log`。

```text
multi: n8=64 n16=32 4 jobs PASS err=0 jobs[0 0 0 0] seqResult=0 jobEnd=4 seqEnd=2 1056 us
cpu: n=64 sync=258113 cyc (537 us) async-poll=254169 cyc (529 us) err=0 => PASS
hint: 中断模式 seqResult=1 jobResult=0 145875 us -> FAIL
det: +2 (期望 2)
```

关键数据：4 Job 序列 1056 us 完成，四个 Job 结果均 SPI_JOB_OK（0），JobEnd 回调 4 次；
同步 258113 周期 vs 异步轮询 254169 周期（差 1.5%），说明 **ASYNC_MODE_POLLING 并不省 CPU**，
它只是把阻塞点从 API 挪到 MainFunction；要省 CPU 必须用中断模式。

## 关键点

1. `Spi_AsyncTransmit` 返回 E_OK 后 `Spi_GetSequenceResult` 必须是 SPI_SEQ_PENDING（1），
   这是例程的断言之一（证明真的异步）。
2. 序列里 Job 顺序为"从机 Job → 主机 Job"，与 spi_master_slave 相同。
3. `spi_job hint` 目前 FAIL：中断模式的 END 中断与 DMA TC 收尾顺序未对齐，属于已知限制。

## 待确认项

- 中断模式：已知限制，原因见上；复跑命令
  `bash scripts/hwrun.sh mcal/spi_async_job --secs 20 --send 'spi_job hint 64'`。
- `spi_job det` 期望 2 条：SetAsyncMode 非法模式在 SWS R25-11 无对应错误码，只返回 E_NOT_OK 不报 Det。
