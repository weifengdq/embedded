/**
 * @file  arch/cc.h
 * @brief lwIP 编译器/平台适配 (GCC, 小端)
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LWIP_ERR_T  int
#define BYTE_ORDER  LITTLE_ENDIAN

#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP ASSERT: %s @ %s:%d\r\n", x, __FILE__, __LINE__); for (;;) { } } while (0)

#define LWIP_RAND()             ((u32_t)rand())

#ifndef PACK_STRUCT_BEGIN
#define PACK_STRUCT_BEGIN
#endif
#ifndef PACK_STRUCT_STRUCT
#define PACK_STRUCT_STRUCT __attribute__((packed))
#endif
#ifndef PACK_STRUCT_END
#define PACK_STRUCT_END
#endif
#ifndef PACK_STRUCT_FIELD
#define PACK_STRUCT_FIELD(x) x
#endif

#endif /* LWIP_ARCH_CC_H */
