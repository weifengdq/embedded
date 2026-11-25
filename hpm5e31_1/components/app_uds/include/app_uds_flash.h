/*
 * app_uds_flash.h - UDS 0x34/0x36/0x37 的刷写后端接口
 *
 * 服务端只依赖这张函数表，因此同一份 UDS 代码可以：
 *   - 在 hpm5e31_1 上用 app_uds_flash_default()（片内 XPI NOR + ROM API）
 *   - 在主机单元测试里换成内存缓冲实现
 *   - 将来接外部 Flash / EEPROM 也只要再写一张表
 *
 * 安全约束（第 31 章"易错点"里有完整讨论）：
 *   写入区必须在 **正在执行的应用镜像之外**，否则擦除会把正在 XIP 取指的代码擦掉；
 *   hpm5e31_1 的 1 MB 片内 Flash 布局为 nor_cfg_option@0x400 / boot_header@0x1000 /
 *   应用@0x3000，示例把下载区固定放在最后 64 KB（0x800F0000~0x80100000）。
 */
#ifndef APP_UDS_FLASH_H
#define APP_UDS_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    /* 准备下载：擦除 [addr, addr+size)，返回 0 成功 */
    int  (*begin)(void *ctx, uint32_t addr, uint32_t size);
    /* 写入一块（addr 为绝对地址），返回 0 成功 */
    int  (*write)(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len);
    /* 结束下载（可选，可为 NULL） */
    int  (*finish)(void *ctx);
    /* 读回并算 CRC32 */
    int  (*crc32)(void *ctx, uint32_t addr, uint32_t size, uint32_t *out);
    /* 单次 0x36 允许的最大块（含 SID+seq，即 maxNumberOfBlockLength） */
    uint32_t (*max_block)(void *ctx);
} app_uds_flash_driver_t;

/* 片内 XPI NOR 实现（hpm5e31_1） */
const app_uds_flash_driver_t *app_uds_flash_default(void);
/* 下载区窗口 */
void app_uds_flash_window(uint32_t *start, uint32_t *end);
/* 最近一次下载的擦除/写入耗时与字节数（用于 0x37 后打印速度） */
void app_uds_flash_stats(uint32_t *erase_us, uint32_t *write_us, uint32_t *programmed);

#ifdef __cplusplus
}
#endif

#endif /* APP_UDS_FLASH_H */
