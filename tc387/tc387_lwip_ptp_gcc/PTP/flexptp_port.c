#include "flexptp_port.h"

#include "Configuration.h"
#include "IfxCpu.h"
#include "IfxPort.h"
#include "IfxScuCcu.h"
#include "Ifx_Lwip.h"
#include "UART_Logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FLEXPTP_PPS_HW_PIN_PORT (&MODULE_P14)
#define FLEXPTP_PPS_HW_PIN_INDEX 4
#define FLEXPTP_PPS_SW_PIN_PORT (&MODULE_P20)
#define FLEXPTP_PPS_SW_PIN_INDEX 2
#define FLEXPTP_PPS_ALIGN_GUARD_NS 100000000ULL

typedef struct
{
    boolean irqState;
    uint32_t lockDepth;
} FlexPtp_OslessLock;

static FlexPtp_OslessLock g_flexPtpLock;

static uint8_t  g_ppsEnabled;
static uint8_t  g_ppsOutputHigh;
static uint32_t g_ppsFreqHz = 1U;
static uint32_t g_ppsDutyPct = 50U;
static uint32_t g_ppsPeriodNs = 1000000000U;
static uint32_t g_ppsHighNs = 500000000U;
static uint8_t  g_ppsNeedReconfig;
static uint64_t g_ppsTargetTotalNs;
static uint32_t g_ppsLastEventTick;

static int64_t flexptp_normalize_ns(int64_t seconds, int64_t nanoseconds, uint32_t *outSeconds, uint32_t *outNanoseconds)
{
    if ((outSeconds == NULL_PTR) || (outNanoseconds == NULL_PTR))
    {
        return -1;
    }

    while (nanoseconds < 0)
    {
        if (seconds <= 0)
        {
            seconds = 0;
            nanoseconds = 0;
            break;
        }

        seconds--;
        nanoseconds += 1000000000LL;
    }

    while (nanoseconds >= 1000000000LL)
    {
        seconds++;
        nanoseconds -= 1000000000LL;
    }

    if (seconds < 0)
    {
        seconds = 0;
    }
    else if ((uint64_t)seconds > 0xFFFFFFFFULL)
    {
        seconds = 0xFFFFFFFFLL;
    }

    *outSeconds = (uint32_t)seconds;
    *outNanoseconds = (uint32_t)nanoseconds;
    return 0;
}

static void flexptp_write_addend(uint32_t addend)
{
    uint32_t attempt;

    for (attempt = 0U; attempt < 8U; ++attempt)
    {
        while (GETH_MAC_TIMESTAMP_CONTROL.B.TSADDREG != 0U)
        {
        }

        GETH_MAC_TIMESTAMP_ADDEND.U = addend;
        GETH_MAC_TIMESTAMP_CONTROL.B.TSADDREG = 1U;
    }

    while (GETH_MAC_TIMESTAMP_CONTROL.B.TSADDREG != 0U)
    {
    }
}

static uint64_t ptphw_gettime_total_ns(void)
{
    TimestampU timestamp;
    ptphw_gettime(&timestamp);
    return ((uint64_t)timestamp.sec * 1000000000ULL) + (uint64_t)timestamp.nanosec;
}

static void ptphw_pps_set_sw_pin(boolean high)
{
    if (high)
    {
        IfxPort_setPinHigh(FLEXPTP_PPS_SW_PIN_PORT, FLEXPTP_PPS_SW_PIN_INDEX);
    }
    else
    {
        IfxPort_setPinLow(FLEXPTP_PPS_SW_PIN_PORT, FLEXPTP_PPS_SW_PIN_INDEX);
    }
}

static void ptphw_pps_clear_status(void)
{
    GETH_MAC_TIMESTAMP_STATUS.U = (uint32_t)(1UL << 1U);
}

static void ptphw_pps_arm_target_total_ns(uint64_t totalNs)
{
    uint32_t targetSeconds = (uint32_t)(totalNs / 1000000000ULL);
    uint32_t targetNanoseconds = (uint32_t)(totalNs % 1000000000ULL);

    while (GETH_MAC_PPS0_TARGET_TIME_NANOSECONDS.B.TRGTBUSY0 != 0U)
    {
    }

    GETH_MAC_PPS0_TARGET_TIME_NANOSECONDS.B.TTSL0 = targetNanoseconds;
    GETH_MAC_PPS0_TARGET_TIME_SECONDS.U = targetSeconds;

    while (GETH_MAC_PPS0_TARGET_TIME_NANOSECONDS.B.TRGTBUSY0 != 0U)
    {
    }

    ptphw_pps_clear_status();
    g_ppsTargetTotalNs = totalNs;
}

static void ptphw_pps_arm_initial_target(void)
{
    uint64_t nowTotalNs = ptphw_gettime_total_ns();
    uint64_t periodNs = g_ppsPeriodNs;
    uint64_t nextTargetNs;

    nextTargetNs = ((nowTotalNs / periodNs) + 1ULL) * periodNs;
    if ((nextTargetNs - nowTotalNs) < FLEXPTP_PPS_ALIGN_GUARD_NS)
    {
        nextTargetNs += periodNs;
    }

    nextTargetNs += periodNs;
    ptphw_pps_arm_target_total_ns(nextTargetNs);
    g_ppsOutputHigh = 0U;
    ptphw_pps_set_sw_pin(FALSE);
}

static void ptphw_pps_handle_event(void)
{
    uint32_t deltaNs;

    if (g_ppsOutputHigh == 0U)
    {
        g_ppsOutputHigh = 1U;
        ptphw_pps_set_sw_pin(TRUE);
        deltaNs = g_ppsHighNs;
    }
    else
    {
        g_ppsOutputHigh = 0U;
        ptphw_pps_set_sw_pin(FALSE);
        deltaNs = g_ppsPeriodNs - g_ppsHighNs;
    }

    ptphw_pps_arm_target_total_ns(g_ppsTargetTotalNs + (uint64_t)deltaNs);
    g_ppsLastEventTick = g_TickCount_1ms;
}

static void ptphw_pps_hw_configure(void)
{
    GETH_MAC_PPS_CONTROL.B.PPSEN0 = 0U;
    GETH_MAC_PPS_CONTROL.B.PPSCTRL_PPSCMD = 0U;
    GETH_MAC_PPS_CONTROL.B.TRGTMODSEL0 = 2U;

    ptphw_pps_arm_initial_target();
    GETH_MAC_PPS_CONTROL.B.PPSEN0 = 1U;

    g_ppsNeedReconfig = 0U;
    g_ppsLastEventTick = g_TickCount_1ms;

    flexptp_printf("PPS: hw=P14.4 sw=P20.2 freq=%luHz duty=%lu%% target=%lu.%09lu\n",
                   (unsigned long)g_ppsFreqHz,
                   (unsigned long)g_ppsDutyPct,
                   (unsigned long)(g_ppsTargetTotalNs / 1000000000ULL),
                   (unsigned long)(g_ppsTargetTotalNs % 1000000000ULL));
}

int flexptp_printf(const char *format, ...)
{
    char formatBuffer[512];
    char txBuffer[768];
    int formattedLength;
    size_t readIndex;
    size_t writeIndex = 0U;
    va_list args;

    if (format == NULL)
    {
        return 0;
    }

    va_start(args, format);
    formattedLength = vsnprintf(formatBuffer, sizeof(formatBuffer), format, args);
    va_end(args);

    if (formattedLength <= 0)
    {
        return formattedLength;
    }

    for (readIndex = 0U; (readIndex < sizeof(formatBuffer)) && (formatBuffer[readIndex] != '\0'); ++readIndex)
    {
        if ((formatBuffer[readIndex] == '\n') && ((readIndex == 0U) || (formatBuffer[readIndex - 1U] != '\r')))
        {
            if ((writeIndex + 1U) >= sizeof(txBuffer))
            {
                break;
            }
            txBuffer[writeIndex++] = '\r';
        }

        if (writeIndex >= (sizeof(txBuffer) - 1U))
        {
            break;
        }

        txBuffer[writeIndex++] = formatBuffer[readIndex];
    }

    txBuffer[writeIndex] = '\0';
    sendUARTMessage(txBuffer, (Ifx_SizeT)writeIndex);
    return formattedLength;
}

void flexptp_osless_lock(bool lock)
{
    if (lock)
    {
        boolean irqState = IfxCpu_disableInterrupts();
        if (g_flexPtpLock.lockDepth == 0U)
        {
            g_flexPtpLock.irqState = irqState;
        }
        g_flexPtpLock.lockDepth++;
    }
    else if (g_flexPtpLock.lockDepth != 0U)
    {
        g_flexPtpLock.lockDepth--;
        if (g_flexPtpLock.lockDepth == 0U)
        {
            IfxCpu_restoreInterrupts(g_flexPtpLock.irqState);
        }
    }
}

void flexptp_ptp_set_addend(uint32_t addend)
{
    flexptp_write_addend(addend);
}

void flexptp_ptp_set_clock(int64_t seconds, int32_t nanoseconds)
{
    uint32_t sec;
    uint32_t nsec;

    if (flexptp_normalize_ns(seconds, nanoseconds, &sec, &nsec) != 0)
    {
        return;
    }

    GETH_MAC_SYSTEM_TIME_SECONDS_UPDATE.U = sec;
    GETH_MAC_SYSTEM_TIME_NANOSECONDS_UPDATE.B.TSSS = nsec;

    while (GETH_MAC_TIMESTAMP_CONTROL.B.TSINIT != 0U)
    {
    }

    GETH_MAC_TIMESTAMP_CONTROL.B.TSINIT = 1U;
    while (GETH_MAC_TIMESTAMP_CONTROL.B.TSINIT != 0U)
    {
    }
}

void ptphw_init(uint32_t increment, uint32_t addend)
{
    float32 gethFreq = IfxScuCcu_getGethFrequency();

    GETH_MAC_TIMESTAMP_CONTROL.U = 0U;
    GETH_MAC_TIMESTAMP_CONTROL.B.SNAPTYPSEL = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSIPV4ENA = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSIPENA = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSVER2ENA = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSEVNTENA = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSCTRLSSR = 1U;
    GETH_MAC_TIMESTAMP_CONTROL.B.TSENA = 1U;

    flexptp_ptp_set_clock(0, 0);
    GETH_MAC_TIMESTAMP_CONTROL.B.TSCFUPDT = 1U;
    flexptp_write_addend(addend);
    /* SUB_SECOND_INCREMENT is not a linear register:
     *   SNSINC = bits [15:8]
     *   SSINC  = bits [23:16]
     * Writing .U = increment only touches reserved low bits and leaves the
     * timestamp counter stalled.  Program the actual fields explicitly. */
    GETH_MAC_SUB_SECOND_INCREMENT.U = 0U;
    GETH_MAC_SUB_SECOND_INCREMENT.B.SNSINC = 0U;
    GETH_MAC_SUB_SECOND_INCREMENT.B.SSINC = (uint8)increment;

    IfxGeth_mac_setAllMulticastPassing(&MODULE_GETH, TRUE);

    flexptp_printf("flexPTP hw init: geth=%.0fHz increment=%lu addend=0x%08lX runtime_ref=%luHz\n",
                   gethFreq,
                   (unsigned long)increment,
                   (unsigned long)addend,
                   (unsigned long)gethFreq);
}

void ptphw_gettime(TimestampU *time_value)
{
    if (time_value == NULL_PTR)
    {
        return;
    }

    do
    {
        time_value->sec = GETH_MAC_SYSTEM_TIME_SECONDS.U;
        time_value->nanosec = GETH_MAC_SYSTEM_TIME_NANOSECONDS.B.TSSS;
    } while (time_value->sec != GETH_MAC_SYSTEM_TIME_SECONDS.U);
}

void ptphw_pps_init_gpio(void)
{
    /* Hardware PPS output: GETH PPS on P14.4 (alt6) */
    IfxPort_setPinModeOutput(FLEXPTP_PPS_HW_PIN_PORT,
                             FLEXPTP_PPS_HW_PIN_INDEX,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_alt6);

    /* Software mirror PPS output: GPIO on P20.2 */
    IfxPort_setPinModeOutput(FLEXPTP_PPS_SW_PIN_PORT,
                             FLEXPTP_PPS_SW_PIN_INDEX,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    ptphw_pps_set_sw_pin(FALSE);
}

void ptphw_pps_set_freq_duty(uint32_t freq_hz, uint32_t duty_pct)
{
    uint64_t highNs;

    if (freq_hz == 0U)
    {
        freq_hz = 1U;
    }
    if (freq_hz > 10000U)
    {
        freq_hz = 10000U;
    }
    if (duty_pct == 0U)
    {
        duty_pct = 1U;
    }
    if (duty_pct > 99U)
    {
        duty_pct = 99U;
    }

    g_ppsFreqHz = freq_hz;
    g_ppsDutyPct = duty_pct;
    g_ppsPeriodNs = 1000000000U / freq_hz;
    if (g_ppsPeriodNs == 0U)
    {
        g_ppsPeriodNs = 1U;
    }

    highNs = ((uint64_t)g_ppsPeriodNs * (uint64_t)duty_pct) / 100ULL;
    if (highNs == 0ULL)
    {
        highNs = 1ULL;
    }
    if (highNs >= (uint64_t)g_ppsPeriodNs)
    {
        highNs = (uint64_t)g_ppsPeriodNs - 1ULL;
    }

    g_ppsHighNs = (uint32_t)highNs;
    g_ppsNeedReconfig = 1U;
}

void ptphw_pps_enable(int enable)
{
    if (enable != 0)
    {
        g_ppsEnabled = 1U;
        ptphw_pps_hw_configure();
    }
    else
    {
        g_ppsEnabled = 0U;
        g_ppsNeedReconfig = 0U;
        GETH_MAC_PPS_CONTROL.B.PPSEN0 = 0U;
        ptphw_pps_set_sw_pin(FALSE);
        flexptp_printf("PPS: hw=P14.4 sw=P20.2 OFF\n");
    }
}

void ptphw_pps_toggle(void)
{
    if (g_ppsEnabled == 0U)
    {
        return;
    }

    if (g_ppsNeedReconfig != 0U)
    {
        ptphw_pps_hw_configure();
        return;
    }

    if (GETH_MAC_TIMESTAMP_STATUS.B.TSTARGT0 != 0U)
    {
        ptphw_pps_clear_status();
        ptphw_pps_handle_event();
    }
    else if ((uint32_t)(g_TickCount_1ms - g_ppsLastEventTick) > 2500U)
    {
        g_ppsLastEventTick = g_TickCount_1ms;
        flexptp_printf("PPS WARN: no target event for >2.5s, rearming\n");
        ptphw_pps_arm_initial_target();
    }
}

void ptphw_pps_poll_fast(void)
{
    if ((g_ppsEnabled != 0U) && (g_ppsNeedReconfig == 0U) && (GETH_MAC_TIMESTAMP_STATUS.B.TSTARGT0 != 0U))
    {
        ptphw_pps_clear_status();
        ptphw_pps_handle_event();
    }
}

int ptphw_pps_is_enabled(void)
{
    return (int)g_ppsEnabled;
}

uint32_t ptphw_pps_get_freq(void)
{
    return g_ppsFreqHz;
}

uint32_t ptphw_pps_get_duty(void)
{
    return g_ppsDutyPct;
}
