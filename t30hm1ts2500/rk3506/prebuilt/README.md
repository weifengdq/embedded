# prebuilt/s2500.ko

这是 `drivers/s2500` 源码的预编译产物，方便在没有 SDK/工具链的机器上先做快速验证。

| 项目 | 值 |
| --- | --- |
| 目标内核 | Linux 6.1.118 SMP PREEMPT，ARMv7（thumb2） |
| vermagic | `6.1.118 SMP preempt mod_unload ARMv7 thumb2 p2v8` |
| 模块版本 | 1.0 |
| 处理方式 | `strip --strip-debug`（去掉调试信息，保留符号表） |

注意：

- **针对某内核版本编译，仅供快速验证；正式使用请按 ../README.md 重编。**
- 加载前先核对 vermagic：

  ```bash
  modinfo prebuilt/s2500.ko | grep vermagic
  uname -r
  ```

  与板端内核不一致时会报 `invalid module format`。
- 预编译文件不会随后续源码修改自动更新，以源码为准。
