/*
 * pinmux_core.h - tp-core 负责外设(GPIO/UART/I2C/SPI)的引脚复用补充声明
 *
 * 该目录下 pinmux_*.c 由 boards/hpm5e31_1/CMakeLists.txt 的 file(GLOB) 自动编译,
 * 新增文件不需要修改 CMakeLists.txt.
 */
#ifndef HPM_PINMUX_CORE_H
#define HPM_PINMUX_CORE_H

#include "hpm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UART4 内部回环(自环)引脚: TX/RX PAD 打开 LOOP_BACK, 外部引脚仍输出/输入但内部可见 */
void init_uart4_loopback_pins(void);
/* UART4 常规引脚(PC16 TXD / PC17 RXD, 与 hpm5e00evk 一致) */
void init_uart4_pins_core(void);

/* SPI1 回环/测试引脚: SCLK 带 LOOP_BACK, MISO/MOSI 打开上拉便于悬空时读到稳定电平 */
void init_spi1_loopback_pins(void);

/* I2C1 引脚(PD13 SCL / PD12 SDA): I2C0 的 PC08 与 MCAN2_TXD 冲突, 故 I2C 测试用 I2C1 */
void init_i2c1_pins_core(void);
/* I2C0 回环/开漏引脚(PC08 SCL / PC09 SDA), 与板上 init_i2c0_pins() 等价但独立命名 */
void init_i2c0_loopback_pins(void);

/* 免费调试 GPIO: PF14/PF15(板上有排针), 用于翻转/示波器测量 */
void init_gpio_test_pins(void);

/* 8 颗 CAN 状态灯(PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19) 全设为 GPIO 输出 */
void init_canled_bank_pins(void);


/* 双 SPI 实例主从实测: SPI1 master / SPI3 slave (用户杜邦线互连) */
void init_spi1_master_pins(void);
void init_spi3_slave_pins(void);
void init_spi3_master_pins(void);
void init_spi1_slave_pins(void);

/* 双 I2C 实例主从实测: I2C2 master(PD02/PD03) / I2C3 slave(PD07/PD06) */
void init_i2c2_master_pins(void);
void init_i2c3_slave_pins(void);
#ifdef __cplusplus
}
#endif
#endif /* HPM_PINMUX_CORE_H */
