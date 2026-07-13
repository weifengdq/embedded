#include "flexptp_app.h"

#include "flexptp_port.h"
#include "Ifx_Lwip.h"

#include "flexptp/cli_cmds.h"
#include "flexptp/logging.h"
#include "flexptp/profiles.h"
#include "flexptp/ptp_profile_presets.h"
#include "flexptp/settings_interface.h"
#include "flexptp/task_ptp.h"

static uint8_t gFlexPtpCliRegistered;
static uint32_t gFlexPtpLastHeartbeatTick;
static FlexPTP_Mode gFlexPtpMode = FLEXPTP_MODE_NONE;

static void flexptp_register_core_commands(void)
{
    if (gFlexPtpCliRegistered == 0U)
    {
        ptp_register_cli_commands();
        gFlexPtpCliRegistered = 1U;
    }
}

int FlexPTP_AppInit(void)
{
    flexptp_register_core_commands();
    gFlexPtpLastHeartbeatTick = g_TickCount_1ms;
    ptphw_pps_init_gpio();
    flexptp_printf("flexPTP CLI ready, use 'ptp on' (default PTP) or 'ptp gptp' (802.1AS)\n");
    flexptp_printf("  PPS: 'ptp pps on|off|freq N|duty N' drives P14.4 from MAC target-time events\n");
    return 0;
}

int FlexPTP_IsRunning(void)
{
    return is_flexPTP_operating() ? 1 : 0;
}

FlexPTP_Mode FlexPTP_GetMode(void)
{
    return gFlexPtpMode;
}

static int flexptp_start_internal(FlexPTP_Mode mode)
{
    flexptp_register_core_commands();

    if (is_flexPTP_operating())
    {
        flexptp_printf("flexPTP is already running, use 'ptp off' first\n");
        return 0;
    }

    gFlexPtpMode = mode;
    gFlexPtpLastHeartbeatTick = g_TickCount_1ms;

    if (!reg_task_ptp())
    {
        gFlexPtpMode = FLEXPTP_MODE_NONE;
        flexptp_printf("flexPTP start failed\n");
        return -1;
    }

    flexptp_printf("flexPTP started (%s mode)\n",
                   (mode == FLEXPTP_MODE_GPTP) ? "gPTP" : "default PTP");
    return 0;
}

int FlexPTP_Start(void)
{
    return flexptp_start_internal(FLEXPTP_MODE_DEFAULT_PTP);
}

int FlexPTP_StartGptp(void)
{
    return flexptp_start_internal(FLEXPTP_MODE_GPTP);
}

int FlexPTP_Stop(void)
{
    if (!is_flexPTP_operating())
    {
        flexptp_printf("flexPTP is already stopped\n");
        return 0;
    }

    unreg_task_ptp();
    gFlexPtpMode = FLEXPTP_MODE_NONE;
    ptphw_pps_enable(0);
    flexptp_printf("flexPTP stopped\n");
    return 0;
}

void FlexPTP_AppProcess(void)
{
    uint32_t now;

    if (!is_flexPTP_operating())
    {
        return;
    }

    now = g_TickCount_1ms;

    while ((uint32_t)(now - gFlexPtpLastHeartbeatTick) >= PTP_HEARTBEAT_TICKRATE_MS)
    {
        gFlexPtpLastHeartbeatTick += PTP_HEARTBEAT_TICKRATE_MS;
        ptp_heartbeat_tmr_cb();
        ptphw_pps_toggle();
    }

    task_ptp();
}

void FlexPTP_PrintStatus(void)
{
    const char *modeStr = "off";

    if (is_flexPTP_operating())
    {
        modeStr = (gFlexPtpMode == FLEXPTP_MODE_GPTP) ? "gPTP" : "PTP";
    }

    flexptp_printf("PTP=%s mode=%s link=%s pps=%s\n",
                   is_flexPTP_operating() ? "on" : "off",
                   modeStr,
                   netif_is_link_up(Ifx_Lwip_getNetIf()) ? "up" : "down",
                   ptphw_pps_is_enabled() ? "on" : "off");

    if (is_flexPTP_operating())
    {
        ptp_print_profile();
    }
    else
    {
        flexptp_printf("Use 'ptp on' (default PTP) or 'ptp gptp' (802.1AS) to start\n");
    }
}

void flexptp_user_event_cb(PtpUserEventCode event_code)
{
    switch (event_code)
    {
    case PTP_UEV_INIT_DONE:
        if (gFlexPtpMode == FLEXPTP_MODE_GPTP)
        {
            flexptp_printf("flexPTP init done, loading gPTP preset (802.1AS P2P over L2)\n");
            ptp_load_profile(ptp_profile_preset_get("gPTP"));
            ptp_set_profile_flags(PTP_PF_SLAVE_ONLY |
                                  PTP_PF_ISSUE_SYNC_FOR_COMPLIANT_SLAVE_ONLY_IN_P2P);
            ptp_set_transport_type(PTP_TP_802_3);
        }
        else
        {
            flexptp_printf("flexPTP init done, loading default PTP preset (E2E over L2)\n");
            ptp_load_profile(ptp_profile_preset_get("default"));
            ptp_set_profile_flags(PTP_PF_SLAVE_ONLY);
            ptp_set_transport_type(PTP_TP_802_3);
        }

        ptp_log_enable(PTP_LOG_LOCKED_STATE, true);
        ptp_log_enable(PTP_LOG_BMCA, false);
        ptp_log_enable(PTP_LOG_INFO, false);
        ptp_print_profile();
        break;

    case PTP_UEV_RESET_DONE:
        flexptp_printf("flexPTP reset done\n");
        break;

    case PTP_UEV_LOCKED:
        flexptp_printf("flexPTP locked (%s)\n",
                       (gFlexPtpMode == FLEXPTP_MODE_GPTP) ? "gPTP" : "PTP");
        break;

    case PTP_UEV_UNLOCKED:
        flexptp_printf("flexPTP unlocked\n");
        break;

    case PTP_UEV_NETWORK_ERROR:
        flexptp_printf("flexPTP network error\n");
        break;

    case PTP_UEV_QUEUE_ERROR:
        flexptp_printf("flexPTP queue error\n");
        break;

    default:
        break;
    }
}
