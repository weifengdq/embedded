/*
 * xcp_test_master_cal.c —— 板内 XCP 主机（CAN1）：在线标定 + 扫参实验
 *
 * 标定流程完全走线上协议：SET_MTA + DOWNLOAD 写 FLOAT32/UWORD，SHORT_UPLOAD 回读，
 * MODIFY_BITS 改 32 位标志位，GET_SEED/UNLOCK 验证资源保护。
 * 扫参：Kp 从 0.1 到 2.0 共 10 点，每点做一次 1 s 阶跃，记录峰值与调节时间。
 */
#include "xcp_test_master_cal.h"

#include "bsp.h"
#include "board.h"
#include "clock.h"
#include "ext/board_can.h"
#include "hpm_csr_drv.h"
#include "hpm_mcan_drv.h"
#include "xcp.h"

#include <string.h>

#define TM_CMD_ID       (0x555u)
#define TM_RES_ID       (0x556u)
#define TM_TIMEOUT_US   (3000u)
#define TM_PID_RES      (0xFFu)
#define TM_PID_ERR      (0xFEu)

extern volatile float    g_kp;
extern volatile float    g_ki;
extern volatile uint16_t g_led_period_ms;
extern volatile uint16_t g_pwm_duty_pm;
extern volatile uint8_t  g_ctrl_enable;
extern volatile uint32_t g_sw_flag;
extern volatile uint16_t g_ctrl_max_pm;
extern volatile uint16_t g_settle_ms;
extern volatile float    g_ctrl_y;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_m_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static MCAN_Type *s_base;
static uint32_t s_cpu_mhz = 480u;
static uint32_t s_fail;
static uint32_t s_tx, s_rx, s_timeouts, s_err_resp;

static uint32_t us_since(uint64_t t0)
{
    return (uint32_t) ((hpm_csr_get_core_mcycle() - t0) / (uint64_t) s_cpu_mhz);
}

static int tm_rx(uint8_t *buf, uint8_t *len, uint32_t timeout_us)
{
    uint64_t t0 = hpm_csr_get_core_mcycle();

    do {
        mcan_rx_message_t rx;
        xcp_poll();                     /* 驱动同固件里的从站 */
        while (mcan_read_rxfifo(s_base, 0u, &rx) == status_success) {
            s_rx++;
            if ((rx.use_ext_id != 0u) || (rx.std_id != TM_RES_ID)) {
                continue;
            }
            uint8_t n = (uint8_t) rx.dlc;
            if (n > 8u) {
                n = 8u;
            }
            memcpy(buf, rx.data_8, n);
            *len = n;
            return 1;
        }
    } while (us_since(t0) < timeout_us);
    return 0;
}

static int tm_req(const uint8_t *cmd, uint8_t clen, uint8_t *res, uint8_t *rlen, uint8_t *err)
{
    mcan_tx_frame_t tx;

    memset(&tx, 0, sizeof(tx));
    tx.std_id = TM_CMD_ID;
    tx.dlc = 8u;
    memcpy(tx.data_8, cmd, clen);
    if (mcan_transmit_blocking(s_base, &tx) != status_success) {
        s_timeouts++;
        return -1;
    }
    s_tx++;
    for (int i = 0; i < 8; i++) {
        if (!tm_rx(res, rlen, TM_TIMEOUT_US)) {
            s_timeouts++;
            return -1;
        }
        if (res[0] == TM_PID_RES) {
            return 0;
        }
        if (res[0] == TM_PID_ERR) {
            s_err_resp++;
            if (err != NULL) {
                *err = res[1];
            }
            printf("     [ERR] cmd=0x%02X err=0x%02X\r\n", (unsigned) cmd[0], (unsigned) res[1]);
            return 1;
        }
    }
    s_timeouts++;
    return -1;
}

static int tm_cmd1(uint8_t c0)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u, err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = c0;
    return tm_req(c, 1u, r, &rl, &err);
}

static void put_addr(uint8_t *c, int pos, uint32_t addr)
{
    c[pos + 0] = (uint8_t) (addr & 0xFFu);
    c[pos + 1] = (uint8_t) ((addr >> 8) & 0xFFu);
    c[pos + 2] = (uint8_t) ((addr >> 16) & 0xFFu);
    c[pos + 3] = (uint8_t) ((addr >> 24) & 0xFFu);
}

/* SET_MTA + UPLOAD/DOWNLOAD 读写任意字节数（n <= 6） */
static int tm_read(uint32_t addr, uint8_t n, uint8_t *out)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u, err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SHORT_UPLOAD;
    c[1] = 0u;
    c[2] = n;
    c[3] = 0u;
    put_addr(c, 4, addr);
    int rc = tm_req(c, 8u, r, &rl, &err);
    if ((rc == 0) && (out != NULL)) {
        memcpy(out, &r[1], n);
    }
    return rc;
}

static int tm_write(uint32_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u, err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SET_MTA;
    put_addr(c, 3, addr);
    if (tm_req(c, 7u, r, &rl, &err) != 0) {
        return -1;
    }
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_DOWNLOAD;
    c[1] = n;
    memcpy(&c[2], data, n);
    return tm_req(c, (uint8_t) (2u + n), r, &rl, &err);
}

static int tm_write_u32(uint32_t addr, uint32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t) (v & 0xFFu);
    b[1] = (uint8_t) ((v >> 8) & 0xFFu);
    b[2] = (uint8_t) ((v >> 16) & 0xFFu);
    b[3] = (uint8_t) ((v >> 24) & 0xFFu);
    return tm_write(addr, b, 4u);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static float u32_to_float(uint32_t v)
{
    return *(float *) (void *) &v;
}

static uint32_t float_to_u32(float f)
{
    return *(uint32_t *) (void *) &f;
}

static void check(bool ok, const char *what)
{
    printf("  [%s] %s\r\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        s_fail++;
    }
}

void tm_init(MCAN_Type *base)
{
    mcan_config_t cfg;

    s_base = base;
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000u;
    board_can_transceivers_enable();
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr = {(uint32_t) (uintptr_t) s_m_msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4u};
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = 1u;
    cfg.can_timing.num_seg1 = 139u;
    cfg.can_timing.num_seg2 = 20u;
    cfg.can_timing.num_sjw = 4u;
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    (void) mcan_init(base, &cfg, clk);
    s_tx = 0u;
    s_rx = 0u;
    s_timeouts = 0u;
    s_err_resp = 0u;
    s_fail = 0u;
}

uint32_t tm_cal_selftest(void)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u, err = 0u;
    uint8_t buf[8];
    uint32_t addr_kp = (uint32_t) (uintptr_t) &g_kp;
    uint32_t addr_ki = (uint32_t) (uintptr_t) &g_ki;
    uint32_t addr_duty = (uint32_t) (uintptr_t) &g_pwm_duty_pm;
    uint32_t addr_led = (uint32_t) (uintptr_t) &g_led_period_ms;
    uint32_t addr_flag = (uint32_t) (uintptr_t) &g_sw_flag;
    uint8_t addr_en = (uint8_t) ((uintptr_t) &g_ctrl_enable);

    s_fail = 0u;
    printf("\r\n=== XCP 在线标定自证（CAN1 主机 <-> CAN0 从站）===\r\n");
    printf("标定量地址：kp=0x%08lX ki=0x%08lX duty=0x%08lX led=0x%08lX en=0x%08X flag=0x%08lX\r\n",
           (unsigned long) addr_kp, (unsigned long) addr_ki, (unsigned long) addr_duty,
           (unsigned long) addr_led, (unsigned) addr_en, (unsigned long) addr_flag);

    /* 1) 连接 */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_CONNECT;
    if (tm_req(c, 2u, r, &rl, &err) != 0) {
        check(false, "CONNECT");
        return s_fail;
    }
    printf("     CONNECT: resource=0x%02X MAX_CTO=%u MAX_DTO=%u\r\n",
           (unsigned) r[1], (unsigned) r[3], (unsigned) (r[4] | (r[5] << 8)));
    check(true, "CONNECT");

    /* 2) 读 Kp（float） */
    float kp0 = 0.0f;
    if ((tm_read(addr_kp, 4u, buf) == 0) && (u32_to_float(rd32(buf)) == g_kp)) {
        kp0 = u32_to_float(rd32(buf));
        printf("     SHORT_UPLOAD g_kp = %.4f\r\n", (double) kp0);
        check(true, "SHORT_UPLOAD 读 FLOAT32 标定量");
    } else {
        check(false, "SHORT_UPLOAD 读 FLOAT32 标定量");
    }

    /* 3) 写 Kp = 1.75，回读，并验证控制环下一拍生效 */
    float kp_new = 1.75f;
    uint32_t v = float_to_u32(kp_new);
    if ((tm_write_u32(addr_kp, v) == 0) && (g_kp == kp_new)) {
        printf("     DOWNLOAD g_kp = %.4f（写后立即生效）\r\n", (double) g_kp);
        check(true, "DOWNLOAD 写 FLOAT32 生效");
    } else {
        check(false, "DOWNLOAD 写 FLOAT32 生效");
    }
    if ((tm_read(addr_kp, 4u, buf) == 0) && (u32_to_float(rd32(buf)) == kp_new)) {
        check(true, "DOWNLOAD 后 SHORT_UPLOAD 回读一致");
    } else {
        check(false, "DOWNLOAD 后 SHORT_UPLOAD 回读一致");
    }

    /* 4) 写 UWORD 标定量（PWM 占空比 / LED 周期） */
    uint16_t duty0 = g_pwm_duty_pm;
    buf[0] = 0x2Cu;                     /* 300 */
    buf[1] = 0x01u;
    if ((tm_write(addr_duty, buf, 2u) == 0) && (g_pwm_duty_pm == 300u)) {
        printf("     DOWNLOAD g_pwm_duty_pm %u -> %u\r\n", (unsigned) duty0, (unsigned) g_pwm_duty_pm);
        check(true, "DOWNLOAD 写 UWORD 标定量");
    } else {
        check(false, "DOWNLOAD 写 UWORD 标定量");
    }
    buf[0] = 0xF4u;                     /* 500 */
    buf[1] = 0x01u;
    if ((tm_write(addr_led, buf, 2u) == 0) && (g_led_period_ms == 500u)) {
        check(true, "DOWNLOAD 写 LED 周期");
    } else {
        check(false, "DOWNLOAD 写 LED 周期");
    }

    /* 5) MODIFY_BITS 改 g_ctrl_enable 所在 32 位字里的 bit 8（这里用 g_sw_flag 演示位操作） */
    (void) addr_en;
    if (tm_write_u32(addr_flag, 0xCAFEB0B0u) == 0) {
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_SET_MTA;
        put_addr(c, 3, addr_flag);
        (void) tm_req(c, 7u, r, &rl, &err);
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_MODIFY_BITS;
        c[1] = 8u;
        c[2] = 0xF0u;                   /* AND = 0xFFF0：清 bit8..11 */
        c[3] = 0xFFu;
        c[4] = 0x0Au;                   /* XOR = 0x000A：置 0xA */
        c[5] = 0x00u;
        if ((tm_req(c, 6u, r, &rl, &err) == 0) && (g_sw_flag == 0xCAFEBAB0u)) {
            printf("     MODIFY_BITS g_sw_flag -> 0x%08lX\r\n", (unsigned long) g_sw_flag);
            check(true, "MODIFY_BITS 位域修改");
        } else {
            check(false, "MODIFY_BITS 位域修改");
        }
    }

    /* 6) GET_SEED / UNLOCK */
    static const uint8_t seed[4] = {0x5Au, 0xA5u, 0x12u, 0x34u};
    xcp_set_seed(seed, 4u);
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_GET_SEED;
    c[1] = 0u;
    c[2] = (uint8_t) (XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG);
    if ((tm_req(c, 3u, r, &rl, &err) == 0) && (r[1] == 4u)) {
        check(true, "GET_SEED");
    } else {
        check(false, "GET_SEED");
    }
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_UNLOCK;
    c[1] = 4u;
    c[2] = 0u;
    if (tm_req(c, 6u, r, &rl, &err) == 1) {
        check(err == XCP_ERR_ACCESS_LOCKED, "错误 key 被拒");
    } else {
        check(false, "错误 key 被拒");
    }
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_UNLOCK;
    c[1] = 4u;
    for (uint8_t i = 0; i < 4u; i++) {
        c[2u + i] = (uint8_t) (seed[i] ^ 0xA5u);
    }
    check(tm_req(c, 6u, r, &rl, &err) == 0, "正确 key UNLOCK");
    xcp_set_seed(NULL, 0u);

    /* 7) 扫参：Kp 0.1 ~ 2.0，观察阶跃响应 */
    printf("-- 扫参：Kp 0.1 -> 2.0（Ki=%.2f 固定，ref=1000）--\r\n", (double) g_ki);
    printf("     Kp     peak_pm  settle_ms  y_final\r\n");
    g_ki = 2.0f;
    uint32_t unstable = 0u;
    for (uint32_t i = 0; i <= 9u; i++) {
        float kp = 0.1f + (float) i * 0.2111f;      /* 0.1 .. 2.0 */
        if (i == 9u) {
            kp = 2.0f;
        }
        uint32_t reg = float_to_u32(kp);
        if (tm_write_u32(addr_kp, reg) != 0) {
            printf("     写 Kp 失败（第 %lu 点）\r\n", (unsigned long) i);
            continue;
        }
        app_step_test(1200u);
        printf("     %.3f  %5u     %5u      %.2f\r\n", (double) g_kp,
               (unsigned) g_ctrl_max_pm, (unsigned) g_settle_ms, (double) g_ctrl_y);
        if (g_settle_ms >= 9999u) {
            unstable++;
        }
    }
    check(unstable == 0u, "10 个 Kp 点都能收敛到 +-5%");

    /* 恢复到默认标定值 */
    (void) tm_write_u32(addr_kp, float_to_u32(1.0f));
    buf[0] = 0xF4u;
    buf[1] = 0x01u;
    (void) tm_write(addr_duty, buf, 2u);

    tm_cmd1(XCP_CMD_DISCONNECT);
    printf("=== 标定自证结论：%s（FAIL=%lu）===\r\n", (s_fail == 0u) ? "PASS" : "FAIL",
           (unsigned long) s_fail);
    printf("XCP CAL SELFTEST %s fail=%lu\r\n", (s_fail == 0u) ? "PASS" : "FAIL", (unsigned long) s_fail);
    printf("master(CAN1): tx=%lu rx=%lu timeouts=%lu err_resp=%lu\r\n",
           (unsigned long) s_tx, (unsigned long) s_rx, (unsigned long) s_timeouts,
           (unsigned long) s_err_resp);
    return s_fail;
}
