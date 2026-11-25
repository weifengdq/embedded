/*
 * app_someip_wire.h —— SOME/IP 大端序编解码小工具（内部头文件）
 *
 * SOME/IP 所有多字节字段都是**网络字节序（大端）**，这是新手最常见的易错点之一：
 * 直接把 ARM 小端结构体 memcpy 到报文里，主机端读出来就是反的。
 *
 * 本文件只做无分支的字节搬移，编译后是几条 lbu/sb 指令，可以放心放在收包热路径。
 */
#ifndef APP_SOMEIP_WIRE_H
#define APP_SOMEIP_WIRE_H

#include <stdint.h>

static inline uint16_t someip_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t someip_get_u24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static inline uint32_t someip_get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void someip_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static inline void someip_put_u24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v & 0xFFU);
}

static inline void someip_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFFU);
}

/* IEEE 802.3 CRC32（与 zlib.crc32 / Python zlib.crc32 一致），用于 SOME/IP-TP 大包校验 */
uint32_t someip_crc32(const uint8_t *data, uint16_t len);

#endif /* APP_SOMEIP_WIRE_H */
