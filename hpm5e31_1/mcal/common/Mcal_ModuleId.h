/*
 * Mcal_ModuleId.h —— AUTOSAR 模块 ID（uint16）集中定义
 *
 * 来源：AUTOSAR CP R25-11《AUTOSAR_CP_SWS_BSWGeneral》§11.2
 *       "List of Basic Software Modules"（本地抽取文本：
 *       autosar_txt/AUTOSAR_CP_SWS_BSWGeneral.txt 约 4710 行起）。
 *       与 NXP S32K3 RTD / Infineon TC4x MCAL 头文件中的 *_MODULE_ID 交叉核对一致
 *       （I2C 除外：NXP 把 I2C 做成 CDD=255，AUTOSAR 标准值为 113）。
 *
 * 注意：Det_ReportError 的第一个参数必须用这里的值，不能用"自定义编号"。
 */
#ifndef MCAL_MODULE_ID_H
#define MCAL_MODULE_ID_H

/* I/O Drivers */
#define ADC_MODULE_ID       ((uint16)123u)
#define DIO_MODULE_ID       ((uint16)120u)
#define ICU_MODULE_ID       ((uint16)122u)
#define PORT_MODULE_ID      ((uint16)124u)
#define PWM_MODULE_ID       ((uint16)121u)

/* Microcontroller Drivers */
#define GPT_MODULE_ID       ((uint16)100u)
#define MCU_MODULE_ID       ((uint16)101u)
#define WDG_MODULE_ID       ((uint16)102u)

/* Communication Drivers */
#define CAN_MODULE_ID       ((uint16)80u)
#define I2C_MODULE_ID       ((uint16)113u)
#define SPI_MODULE_ID       ((uint16)83u)

/* Communication Hardware Abstraction（以太网） */
#define ETH_MODULE_ID       ((uint16)88u)   /* Ethernet Driver */
#define ETHTRCV_MODULE_ID   ((uint16)73u)   /* Ethernet Transceiver Driver */

/* Memory Drivers */
#define MEM_MODULE_ID       ((uint16)91u)   /* R25-11 起原 Fls(92) 并入 Memory Driver */

/* Libraries */
#define CRC_MODULE_ID       ((uint16)201u)

/* System Services（本仓库只用桩） */
#define DET_MODULE_ID       ((uint16)15u)
#define DEM_MODULE_ID       ((uint16)54u)
#define ECUM_MODULE_ID      ((uint16)10u)
#define SCHM_MODULE_ID      ((uint16)130u)

#endif /* MCAL_MODULE_ID_H */
