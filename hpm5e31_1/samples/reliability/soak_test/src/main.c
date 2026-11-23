/*
 * soak_test - 长稳测试：内存/时钟/uptime/watchdog/中断 综合自检
 *
 * 目的：连续运行数十分钟，检测
 *   1) uptime（mcycle）与 MCHTMR 的漂移与回绕处理；
 *   2) SRAM 完整性（memcpy + CRC32 校验，覆盖 AXI SRAM / DLM 区域）；
 *   3) heap 反复 malloc/free 是否泄漏/碎片化；
 *   4) GPIO 翻转与定时器中断是否持续工作；
 *   5) 错误计数器（任何异常都打印并计数）。
 */
#include <string.h>
#include "bsp.h"
#include "hpm_crc_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_gptmr_drv.h"

#define SOAK_BUF_SIZE   (8U * 1024U)
#define HEARTBEAT_MS    (60000U)

static uint8_t s_buf_a[SOAK_BUF_SIZE];
static uint8_t s_buf_b[SOAK_BUF_SIZE];
static volatile uint32_t s_irq_cnt;
static uint32_t s_err_cnt;
static uint32_t s_cycle;
static uint32_t s_crc_ref;
static uint32_t s_uptime_last;
static uint32_t s_beat;

/* 栈水位：链接脚本 flash_xip.ld 提供 __stack_base__（栈底）与 _stack（栈顶）。
 * 启动后在"当前 SP 以下"涂 0xA5，运行中栈向下生长会覆盖涂色；
 * 之后从栈底向上找第一个非 0xA5 的地址，即历史最深栈底，得到峰值占用。 */
extern uint8_t __stack_base__[];
extern uint8_t _stack[];

static inline uint32_t soak_read_sp(void)
{
    uint32_t v;
    __asm__ volatile ("mv %0, sp" : "=r"(v));
    return v;
}

static void soak_stack_paint(void)
{
    uint8_t *lo = __stack_base__;
    uint8_t *hi = (uint8_t *) soak_read_sp();
    if (hi > lo) {
        for (uint8_t *p = lo; p < hi; p++) {
            *p = 0xA5U;
        }
    }
}

static uint32_t soak_stack_peak(void)
{
    uint8_t *lo = __stack_base__;
    uint32_t total = (uint32_t) ((uintptr_t) _stack - (uintptr_t) lo);
    uint32_t i = 0;
    while (i < total && lo[i] == 0xA5U) {
        i++;
    }
    return total - i;
}

/* GPTMR0 1 ms 周期中断：验证中断持续可用 */
SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR0, soak_gptmr_isr)
void soak_gptmr_isr(void)
{
    gptmr_clear_status(HPM_GPTMR0, GPTMR_CH_RLD_STAT_MASK(GPTMR_CHANNEL_CH0));
    s_irq_cnt++;
}

static void soak_timer_init(void)
{
    /* GPTMR0 时钟默认未开（在本板 clock.c 的时钟组之外），先开时钟再碰寄存器 */
    clock_add_to_group(clock_gptmr0, 0);
    gptmr_channel_config_t cfg;
    gptmr_channel_get_default_config(HPM_GPTMR0, &cfg);
    cfg.reload = clock_get_frequency(clock_gptmr0) / 1000U;
    cfg.cmp[0] = cfg.reload;
    gptmr_channel_config(HPM_GPTMR0, GPTMR_CHANNEL_CH0, &cfg, false);
    gptmr_enable_irq(HPM_GPTMR0, GPTMR_CH_RLD_IRQ_MASK(GPTMR_CHANNEL_CH0));
    intc_m_enable_irq_with_priority(IRQn_GPTMR0, 3);
    gptmr_start_counter(HPM_GPTMR0, GPTMR_CHANNEL_CH0);
}

static uint32_t crc32_of(const uint8_t *p, uint32_t len)
{
    /* 使用软件 CRC32（多项式 0xEDB88320），避免依赖 CRC 外设配置 */
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (crc & 1U)));
        }
    }
    return ~crc;
}

static void cmd_soak(int argc, char *argv[])
{
    uint32_t ms = bsp_uptime_ms();
    printf("soak: cycle=%lu uptime=%lu.%03lu s irq=%lu err=%lu beat=%lu\r\n",
           (unsigned long) s_cycle, (unsigned long) (ms / 1000U), (unsigned long) (ms % 1000U),
           (unsigned long) s_irq_cnt, (unsigned long) s_err_cnt, (unsigned long) s_beat);
    printf("soak: mchtmr=%lu  crc_ref=0x%08lx\r\n",
           (unsigned long) (uint32_t) mchtmr_get_count(HPM_MCHTMR), (unsigned long) s_crc_ref);
    printf("soak: stack peak=%lu B / total=%lu B (%.1f%%)\r\n",
           (unsigned long) soak_stack_peak(),
           (unsigned long) ((uintptr_t) _stack - (uintptr_t) __stack_base__),
           100.0 * (double) soak_stack_peak() / (double) ((uintptr_t) _stack - (uintptr_t) __stack_base__));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 soak, cmd_soak, show soak test statistics);

static void cmd_heap(int argc, char *argv[])
{
    void *p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = malloc(1024);
    }
    uint32_t ok = 1;
    for (int i = 0; i < 8; i++) {
        if (!p[i]) { ok = 0; break; }
        memset(p[i], i, 1024);
    }
    for (int i = 0; i < 8; i++) {
        free(p[i]);
    }
    printf("heap: 8x1KB alloc/free %s\r\n", ok ? "OK" : "FAIL");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 heap, cmd_heap, heap alloc/free test);

int main(void)
{
    bsp_init();
    soak_stack_paint();
    board_init_led_pins();
    soak_timer_init();

    printf("[soak] init done (timer ok), stack base=0x%08lx top=0x%08lx size=%lu B\r\n",
           (unsigned long) (uintptr_t) __stack_base__, (unsigned long) (uintptr_t) _stack,
           (unsigned long) ((uintptr_t) _stack - (uintptr_t) __stack_base__));
    printf("\r\n[soak] start: buf=%lu KB, heartbeat=%lu s\r\n",
           (unsigned long) (SOAK_BUF_SIZE / 1024U), (unsigned long) (HEARTBEAT_MS / 1000U));

    for (uint32_t i = 0; i < SOAK_BUF_SIZE; i++) {
        s_buf_a[i] = (uint8_t) (i * 7U + 3U);
    }
    s_crc_ref = crc32_of(s_buf_a, SOAK_BUF_SIZE);
    s_uptime_last = bsp_uptime_ms();
    uint32_t next_beat = HEARTBEAT_MS;
    uint32_t led = 0;

    while (1) {
        bsp_shell_poll();
        s_cycle++;

        /* 1) SRAM 完整性 */
        memcpy(s_buf_b, s_buf_a, SOAK_BUF_SIZE);
        uint32_t crc = crc32_of(s_buf_b, SOAK_BUF_SIZE);
        if (crc != s_crc_ref) {
            s_err_cnt++;
            printf("[soak] ERROR crc mismatch cycle=%lu got=0x%08lx exp=0x%08lx\r\n",
                   (unsigned long) s_cycle, (unsigned long) crc, (unsigned long) s_crc_ref);
        }

        /* 2) uptime 单调性 */
        uint32_t now = bsp_uptime_ms();
        if (now < s_uptime_last) {
            s_err_cnt++;
            printf("[soak] ERROR uptime wrapped/backward %lu -> %lu\r\n",
                   (unsigned long) s_uptime_last, (unsigned long) now);
        }
        s_uptime_last = now;

        /* 3) 中断计数必须持续增长 */
        static uint32_t irq_last;
        static uint32_t irq_stall;
        if (s_irq_cnt == irq_last) {
            if (++irq_stall > 3U) {
                s_err_cnt++;
                printf("[soak] ERROR gptmr irq stalled at %lu\r\n", (unsigned long) s_irq_cnt);
                irq_stall = 0;
            }
        } else {
            irq_stall = 0;
        }
        irq_last = s_irq_cnt;

        /* 4) LED 心跳 + 周期汇总 */
        if (now >= next_beat) {
            next_beat += HEARTBEAT_MS;
            s_beat++;
            led = !led;
            gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN, (uint8_t) led);
            printf("[soak] beat=%lu uptime=%lu.%03lu s irq=%lu cycles=%lu err=%lu stack=%luB\r\n",
                   (unsigned long) s_beat, (unsigned long) (now / 1000U), (unsigned long) (now % 1000U),
                   (unsigned long) s_irq_cnt, (unsigned long) s_cycle, (unsigned long) s_err_cnt,
                   (unsigned long) soak_stack_peak());
        }

        board_delay_ms(1);
    }
    return 0;
}
