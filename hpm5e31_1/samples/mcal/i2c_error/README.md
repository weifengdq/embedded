# i2c_error —— AUTOSAR MCAL I2c 错误路径例程

验证 MCAL I2c 的错误分类（SWS I2CDriver §7.5）：NACK、总线被占（仲裁失败类）、
非法参数的 DevError 上报与 Det 计数。

## 硬件要求与接线

同 `i2c_master_slave`：I2C2（SCL PD02 / SDA PD03）↔ I2C3（SCL PD07 / SDA PD06，目标 0x50）。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/i2c_error
bash scripts/hwrun.sh mcal/i2c_error --secs 25 --send 'i2c_err all'
```

| 命令 | 说明 |
|---|---|
| `i2c_err nack` | 访问不存在的地址 0x33，期望 I2C_SEQ_NACK + 运行时错误计数 |
| `i2c_err busy` | 把 SDA 拉低模拟总线被占，期望传输失败并上报 I2C_E_ARBITRATION_FAILURE |
| `i2c_err det` | 8 条非法参数调用，期望 Det 计数 +8 |
| `i2c_err all` | 依次跑上面三项 |
| `i2c_err info` | 当前运行时错误计数、Det 计数、通过次数 |

## 实测现象与数据

串口日志：`logs/20261102-142544-i2c_error.log`。

```text
nack: addr=0x33 SyncTransmit=E_NOT_OK seqResult=3 runtimeErr=1 det=... => PASS (NACK 被识别)
busy: SDA 被拉低后 SyncTransmit=E_NOT_OK seqResult=NACK runtimeErr=2 => PASS (总线故障被识别)
det: DevError +8 (期望 8) 总计数=10 => PASS
i2c_error: done, runtimeErr=2 det=10
```

关键数据：NACK 被识别为 `I2C_SEQ_NACK`（0x03）并上报运行时错误；总线被占同样返回 E_NOT_OK
并累加运行时错误；8 条非法参数全部计进 Det。

## 关键点

1. NACK 与总线故障在 SWS 里属于 **Runtime Error**（I2C_E_NACK_RECEIVED / I2C_E_ARBITRATION_FAILURE），
   走 Det_ReportRuntimeError，不计入 DevError 语义；本仓库 Det 把两类统一计数，便于上板断言。
2. 模拟"总线被占"的做法：把控制器 SDA 接管成 GPIO 输出低电平再发传输。测试完必须重新
   `I2C_DeInit` + `I2C_Init` 恢复引脚复用，否则后续传输会一直失败。
3. 非法模式（SetAsyncMode）在 SWS R25-11 没有对应错误码，驱动只返回 E_NOT_OK，不报 Det。

## 待确认项

- 真实多主仲裁冲突未验证：需要第二颗 MCU 或 I2C 主设备同时发时钟，本板单机无法构造；
  当前用"SDA 被拉低"等效覆盖总线故障路径。复跑命令
  `bash scripts/hwrun.sh mcal/i2c_error --secs 20 --send 'i2c_err busy'`。
