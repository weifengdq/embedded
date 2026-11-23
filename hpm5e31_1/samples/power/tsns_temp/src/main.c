/*
 * tsns_temp - 片内温度传感器 TSNS 读数、统计与温度阈值告警
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无需外部接线
 *   TSNS 是独立于 ADC 的片内温度传感器, 温度 = raw / 256 (TSNS_TEMP_SCALE)
 *   出厂校准数据在 OTP shadow 21 (TSNS 校准字), 若未校准需要软件标定(见 SDK tsns 例程)
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_tsns_drv.h"
#include "hpm_otp_drv.h"

#define APP_TSNS            HPM_TSNS
#define OTP_SHADOW_TSNS     (21U)
#define OTP_TSNS_BASE_MASK  ((1 << 12) - 1)

static uint32_t s_otp_tsns;

static void cmd_temp(void)
{
    int32_t raw = tsns_get_current_temp_in_raw(APP_TSNS);
    printf("TSNS raw=0x%08lx (%ld) -> %.2f C, age=%lu\r\n",
           (unsigned long) raw, (long) raw,
           (double) ((float) raw / (float) TSNS_TEMP_SCALE),
           (unsigned long) tsns_get_temp_age(APP_TSNS));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 temp, cmd_temp, read TSNS temperature once);

static void cmd_tempsr(void)
{
    printf("TSNS @0x%08lx\r\n", (unsigned long) APP_TSNS);
    printf("  scale      : 1/256 C per LSB (TSNS_TEMP_SCALE=%d)\r\n", (int) TSNS_TEMP_SCALE);
    printf("  continuous : enabled, validity bits set by HW\r\n");
    printf("  OTP shadow21(TSNS cal): 0x%08lx  base=0x%03lx  en=%lu\r\n",
           (unsigned long) s_otp_tsns,
           (unsigned long) (s_otp_tsns & OTP_TSNS_BASE_MASK),
           (unsigned long) ((s_otp_tsns >> 24) & 1U));
    printf("  max raw=0x%08lx  min raw=0x%08lx\r\n",
           (unsigned long) (uint32_t) tsns_get_max_temp_raw(APP_TSNS),
           (unsigned long) (uint32_t) tsns_get_min_temp_raw(APP_TSNS));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tempsr, cmd_tempsr, TSNS configuration / OTP calibration summary);

static void cmd_tempstat(int argc, char *argv[])
{
    uint32_t n = 10;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &n) || n == 0 || n > 1000)) {
        printf("usage: tempstat [1..1000] (100ms interval)\r\n");
        return;
    }
    float tmin = 1e9f, tmax = -1e9f, sum = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float t = tsns_get_current_temp(APP_TSNS);
        if (t < tmin) tmin = t;
        if (t > tmax) tmax = t;
        sum += t;
        board_delay_ms(100);
    }
    printf("tempstat: n=%lu (%.1f s) min=%.2f C max=%.2f C avg=%.2f C pp=%.2f C\r\n",
           (unsigned long) n, (double) n * 0.1,
           (double) tmin, (double) tmax, (double) (sum / (float) n), (double) (tmax - tmin));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tempstat, cmd_tempstat, temperature statistics: tempstat [n]);

int main(void)
{
    bsp_init();

    printf("\r\n=== tsns_temp: on-chip temperature sensor ===\r\n");

    clock_add_to_group(clock_tsns, 0);
    s_otp_tsns = otp_read_from_shadow(OTP_SHADOW_TSNS);
    tsns_enable_continuous_mode(APP_TSNS);
    tsns_enable(APP_TSNS);
    cmd_tempsr();

    printf("shell: temp | tempstat [n] | tempsr\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 1000U) {
            last = bsp_uptime_ms();
            int32_t raw = tsns_get_current_temp_in_raw(APP_TSNS);
            printf("t=%5lu ms  temp=%6.2f C  raw=0x%04lx\r\n",
                   (unsigned long) last,
                   (double) ((float) raw / (float) TSNS_TEMP_SCALE),
                   (unsigned long) (raw & 0xFFFF));
        }
    }
    return 0;
}
