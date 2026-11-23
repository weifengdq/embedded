/*
 * flash_boot_layout - 打印片内 Flash 的启动镜像布局 (只读, 不擦写)
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 片内 1MB Flash 映射在 0x80000000 (XPI0)
 *   读取 nor_cfg_option(0x80000400) 与 boot_header(0x80001000), 解出 fw 表并打印布局表;
 *   与链接脚本 etext 对比, 得出镜像实际占用。
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_bootheader.h"

#define APP_FLASH_BASE       (0x80000000UL)
#define APP_FLASH_SIZE       (1024UL * 1024UL)
#define APP_NOR_CFG_OFFSET   (0x400UL)
#define APP_BOOT_HDR_OFFSET  (0x1000UL)
#define APP_SAFE_OFFSET      (0xF0000UL)

extern char etext[];

static void print_layout(void)
{
    const uint32_t *cfg = (const uint32_t *) (uintptr_t) (APP_FLASH_BASE + APP_NOR_CFG_OFFSET);
    const boot_header_t *bh = (const boot_header_t *) (uintptr_t) (APP_FLASH_BASE + APP_BOOT_HDR_OFFSET);
    const fw_info_table_t *fw = (const fw_info_table_t *) (uintptr_t) (APP_FLASH_BASE + APP_BOOT_HDR_OFFSET + 0x10);
    uint32_t img_end = (uint32_t) (uintptr_t) etext;

    printf("Flash base=0x%08lx size=%lu KB\r\n",
           (unsigned long) APP_FLASH_BASE, (unsigned long) (APP_FLASH_SIZE / 1024U));
    printf("  +0x%04lx nor_cfg_option : %08lx %08lx %08lx %08lx\r\n",
           (unsigned long) APP_NOR_CFG_OFFSET,
           (unsigned long) cfg[0], (unsigned long) cfg[1], (unsigned long) cfg[2], (unsigned long) cfg[3]);
    printf("  +0x%04lx boot_header    : tag=0x%02x ver=0x%02x len=%u fw_count=%u dc_off=%u sig_off=%u\r\n",
           (unsigned long) APP_BOOT_HDR_OFFSET,
           (unsigned) bh->tag, (unsigned) bh->version, (unsigned) bh->length,
           (unsigned) bh->fw_count, (unsigned) bh->dc_block_offset, (unsigned) bh->sig_block_offset);
    printf("  fw[0]: offset=0x%04lx  size=0x%05lx (%lu B)  entry=0x%08lx\r\n",
           (unsigned long) fw->offset, (unsigned long) fw->size, (unsigned long) fw->size,
           (unsigned long) fw->entry_point);
    printf("  app image: 0x%08lx..0x%08lx (etext, %lu KB), boot header says app at +0x%04lx\r\n",
           (unsigned long) APP_FLASH_BASE, (unsigned long) img_end,
           (unsigned long) ((img_end - APP_FLASH_BASE) / 1024U), (unsigned long) fw->offset);
    printf("  safe rw window: +0x%05lx..+0xFFFFF (%lu KB)\r\n",
           (unsigned long) APP_SAFE_OFFSET,
           (unsigned long) ((APP_FLASH_SIZE - APP_SAFE_OFFSET) / 1024U));

    printf("\r\n  布局表:\r\n");
    printf("  | 偏移        | 大小      | 内容                     |\r\n");
    printf("  |-------------|-----------|--------------------------|\r\n");
    printf("  | 0x00000     | 0x400     | 保留                     |\r\n");
    printf("  | 0x00400     | 0x10      | nor_cfg_option (XPI 配置)|\r\n");
    printf("  | 0x01000     | 0x90      | boot_header + fw[0]      |\r\n");
    printf("  | 0x%05lx     | 0x%05lx   | 应用镜像 fw[0]           |\r\n",
           (unsigned long) fw->offset, (unsigned long) fw->size);
    printf("  | 0x%05lx     | 0x%05lx   | 未使用                   |\r\n",
           (unsigned long) (fw->offset + fw->size),
           (unsigned long) (APP_SAFE_OFFSET - (fw->offset + fw->size)));
    printf("  | 0x%05lx     | 0x10000   | 读写测试安全区           |\r\n", (unsigned long) APP_SAFE_OFFSET);
}

static void cmd_layout(void)
{
    print_layout();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 layout, cmd_layout, print flash boot layout);

static void cmd_dump(int argc, char *argv[])
{
    uint32_t off = 0, len = 256;
    if (argc >= 2 && bsp_parse_u32(argv[1], &off)) {
        printf("usage: dump [offset] [len] (offset <= 0xFFFFF)\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &len);
    }
    if (off >= APP_FLASH_SIZE) {
        printf("offset out of range\r\n");
        return;
    }
    if (len > 1024U) {
        len = 1024U;
    }
    if (off + len > APP_FLASH_SIZE) {
        len = APP_FLASH_SIZE - off;
    }
    bsp_hexdump((const void *) (uintptr_t) (APP_FLASH_BASE + off), len);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dump, cmd_dump, hexdump flash: dump [offset] [len]);

int main(void)
{
    bsp_init();
    printf("\r\n=== flash_boot_layout: internal flash image layout (read-only) ===\r\n");
    print_layout();
    printf("shell: layout | dump [offset] [len]\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
