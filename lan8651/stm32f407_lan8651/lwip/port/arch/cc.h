/**
 * @file  cc.h
 * @brief lwIP architecture / compiler abstraction for ARM GCC on STM32
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Tell lwIP about the printf format specifiers */
#define LWIP_NO_STDINT_H    0
#define LWIP_NO_INTTYPES_H  0

/* Use our debug_printf for lwIP diagnostics */
extern int debug_printf(const char *fmt, ...);
#define LWIP_PLATFORM_DIAG(x)   do { debug_printf x ; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { debug_printf("lwIP ASSERT: %s\r\n", x); while(1); } while(0)

/* Byte order — ARM Cortex-M is little-endian */
#define BYTE_ORDER  LITTLE_ENDIAN

/* Compiler hints */
#define LWIP_NO_STDDEF_H  0

/* Align on 4
 * Packing */
#define PACK_STRUCT_FIELD(x)           x
#define PACK_STRUCT_STRUCT             __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* Provide a rand() for lwIP */
#define LWIP_RAND()   ((uint32_t)rand())

#endif /* LWIP_ARCH_CC_H */
