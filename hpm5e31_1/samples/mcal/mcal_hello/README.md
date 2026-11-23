# mcal_hello —— MCAL 公共基础设施自检

## 功能

- 打印 `Mcal_Version.h` 版本、AUTOSAR 模块 ID 表（与 SWS_BSWGeneral §11.3 对照）；
- 打印 `Std_Types.h` 基本类型宽度，验证 32 位 RV32 下 `uint32`=4 字节、`Std_ReturnType`=1 字节；
- 验证 Det（Default Error Tracer）桩：注入一条错误后计数增加、可回读。

## 硬件要求

无外部接线；USR 串口 921600 8N1。

## 运行

```bash
cd hpm5e31_1 && source scripts/env.sh
bash scripts/build.sh samples/mcal/mcal_hello
bash scripts/hwrun.sh mcal/mcal_hello --secs 6 --send "mcal;det"
```

## 命令

| 命令 | 说明 |
|---|---|
| mcal | 打印版本/模块 ID/类型宽度 |
| det | 打印 Det 计数与最后一条错误，并注入一条测试 |
| help | letter-shell 内置 |

## 实测现象

运行后串口输出（见 `logs/` 下对应日志）：

```text
=== mcal_hello: AUTOSAR MCAL common infra self-test ===
MCAL: HPM5E31 MCAL 1.0.0 (AUTOSAR CP R21-11 subset)
module ids: Mcu=101 Port=124 Dio=120 Gpt=100 Pwm=121 Icu=122
            Adc=123 Spi=83 I2c=113 Can=80 Wdg=102 Mem=91 Crc=201 Det=15
DET: count=0
DET: no error recorded
DET: after inject count=1
```
