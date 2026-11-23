/*
 * Platform_Types.h —— AUTOSAR Platform Types（HPM5E31 / RV32 GCC 移植）
 *
 * 依据：AUTOSAR_CP_SWS_PlatformTypes（R21-11/R25-11）。HPM5E31 是 32 位
 * RV32-IMAFDCBP，字节序小端（LSB first / low byte first），char 8 位。
 * 本文件只依赖编译器内置类型，不包含任何 SDK 头文件，保证 MCAL 可独立编译。
 */
#ifndef PLATFORM_TYPES_H
#define PLATFORM_TYPES_H

#define PLATFORM_TYPES_AR_RELEASE_MAJOR_VERSION   (4u)
#define PLATFORM_TYPES_AR_RELEASE_MINOR_VERSION   (4u)
#define PLATFORM_TYPES_AR_RELEASE_REVISION_VERSION (0u)
#define PLATFORM_TYPES_SW_MAJOR_VERSION           (1u)
#define PLATFORM_TYPES_SW_MINOR_VERSION           (0u)
#define PLATFORM_TYPES_SW_PATCH_VERSION           (0u)

/* ---- CPU 类型/字节序：HPM5E31 = RV32，小端 ---- */
#define CPU_TYPE_8      8
#define CPU_TYPE_16     16
#define CPU_TYPE_32     32
#define CPU_TYPE_64     64

#define MSB_FIRST       0
#define LSB_FIRST       1

#define HIGH_BYTE_FIRST 0
#define LOW_BYTE_FIRST  1

#define CPU_TYPE        CPU_TYPE_32
#define CPU_BIT_ORDER   LSB_FIRST
#define CPU_BYTE_ORDER  LOW_BYTE_FIRST

/* ---- 基本类型（AUTOSAR 强制命名，禁止直接用 int/long） ---- */
typedef signed char         sint8;
typedef unsigned char       uint8;
typedef signed short        sint16;
typedef unsigned short      uint16;
typedef signed int          sint32;
typedef unsigned int        uint32;

typedef signed long long    sint64;
typedef unsigned long long  uint64;

typedef float               float32;
typedef double              float64;

typedef unsigned char       boolean;

/* ---- 优化类型：RV32 上 int 为 32 位，least/fast 与基本类型同宽 ---- */
typedef sint8   sint8_least;
typedef uint8   uint8_least;
typedef sint16  sint16_least;
typedef uint16  uint16_least;
typedef sint32  sint32_least;
typedef uint32  uint32_least;

typedef sint8   sint8_fast;
typedef uint8   uint8_fast;
typedef sint16  sint16_fast;
typedef uint16  uint16_fast;
typedef sint32  sint32_fast;
typedef uint32  uint32_fast;

/* ---- 常用边界值 ---- */
#ifndef UINT8_MAX
#define UINT8_MAX   0xFFu
#endif
#ifndef UINT16_MAX
#define UINT16_MAX  0xFFFFu
#endif
#ifndef UINT32_MAX
#define UINT32_MAX  0xFFFFFFFFu
#endif
#ifndef INT32_MAX
#define INT32_MAX   0x7FFFFFFF
#endif

#endif /* PLATFORM_TYPES_H */
