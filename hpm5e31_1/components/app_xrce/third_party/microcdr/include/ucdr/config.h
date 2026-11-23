/*
 * ucdr/config.h —— Micro-CDR 2.0.2 平台配置（手工生成版）
 *
 * 上游由 CMake 用 include/ucdr/config.h.in 生成；本仓库非 CMake 组件式集成，
 * 故固化于此。上游：eProsima Micro-CDR v2.0.2（Apache-2.0）。
 */
#ifndef _MICROCDR_CONFIG_H_
#define _MICROCDR_CONFIG_H_

#define MICROCDR_VERSION_MAJOR 2
#define MICROCDR_VERSION_MINOR 0
#define MICROCDR_VERSION_MICRO 2
#define MICROCDR_VERSION_STR "2.0.2"

/* RISC-V 小端；取值见 microcdr.h 的 ucdrEndianness 枚举（LITTLE=1）。 */
#define UCDR_MACHINE_ENDIANNESS UCDR_LITTLE_ENDIANNESS

#endif /* _MICROCDR_CONFIG_H_ */
