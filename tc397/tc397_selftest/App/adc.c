/* TC397 EVADC driver for AN0..AN47 board monitor.
 *
 * Mapping (LFBGA292 PinMap IfxEvadc_PinMap_TC39xB_LFBGA292):
 *   AN0..AN7   -> G0 CH0..CH7
 *   AN8..AN15  -> G1 CH0..CH7
 *   AN16       -> G2 CH0
 *   AN20..AN23 -> G2 CH4..CH7
 *   AN30..AN31 -> G3 CH6..CH7
 *   AN34..AN35 -> G8 CH2..CH3
 *   AN40..AN47 -> G8 CH8..CH15
 * AN17..19,24..29,32,33,36..39 are P40.x GPIO on this board (not sampled).
 *
 * All used channels run in queue0/refill background scan (gate always),
 * so the shell command only reads the latest result registers.
 */
#include "adc.h"
#include "Evadc/Adc/IfxEvadc_Adc.h"
#include "Evadc/Std/IfxEvadc.h"

/* ---- AN table (index = AN number) ---- */
#define SKIP  0xFF, 0xFF, AdcKind_Skip
#define GCH(g, c) g, c

const AdcAnInfo g_AdcAnTable[48] = {
    /* an  group ch kind            name */
    { 0,  GCH(0, 0), AdcKind_Pin,     "SPARE"     },  /* reserved, pin level only */
    { 1,  GCH(0, 1), AdcKind_Pin,     "SPARE"     },
    { 2,  GCH(0, 2), AdcKind_Pin,     "SPARE"     },
    { 3,  GCH(0, 3), AdcKind_Pin,     "SPARE"     },
    { 4,  GCH(0, 4), AdcKind_Pin,     "SPARE"     },
    { 5,  GCH(0, 5), AdcKind_Pin,     "SPARE"     },
    { 6,  GCH(0, 6), AdcKind_Pin,     "SPARE"     },
    { 7,  GCH(0, 7), AdcKind_Pin,     "SPARE"     },
    { 8,  GCH(1, 0), AdcKind_Div50_3, "VPREREG"   },  /* TLF35584 Buck out, 47K+3K */
    { 9,  GCH(1, 1), AdcKind_Div50_3, "VS1"       },  /* TLF35584 Boost out, 47K+3K */
    {10,  GCH(1, 2), AdcKind_Div50_3, "VBAT"      },  /* ext input power, 47K+3K */
    {11,  GCH(1, 3), AdcKind_Div50_3, "ETH_INH"   },  /* YT8011AN INH, 47K+3K */
    {12,  GCH(1, 4), AdcKind_Div50_3, "CAN0_INH"  },  /* TCAN1043 INH, 47K+3K */
    {13,  GCH(1, 5), AdcKind_Div50_3, "IG"        },  /* ignition, 47K+3K */
    {14,  GCH(1, 6), AdcKind_Div50_3, "HSS0"      },  /* high-side current, 47K+3K */
    {15,  GCH(1, 7), AdcKind_Div50_3, "HSS1"      },  /* high-side current, 47K+3K */
    {16,  GCH(2, 0), AdcKind_Direct,  "VUC"       },  /* TLF35584 QUC, direct */
    {17,  SKIP,                     "P40-GPIO"  },
    {18,  SKIP,                     "P40-GPIO"  },
    {19,  SKIP,                     "P40-GPIO"  },
    {20,  GCH(2, 4), AdcKind_Direct,  "3V3"       },  /* extra 3V3 rail, direct */
    {21,  GCH(2, 5), AdcKind_Direct,  "1V25"      },  /* MCU core voltage, direct */
    {22,  GCH(2, 6), AdcKind_Direct,  "0V9"       },  /* YT8011AN 0.9V, direct */
    {23,  GCH(2, 7), AdcKind_HwVer,   "HW_VERSION"},  /* VUC 10K+1K div, HW1.0 */
    {24,  SKIP,                     "P40-GPIO"  },
    {25,  SKIP,                     "P40-GPIO"  },
    {26,  SKIP,                     "P40-GPIO"  },
    {27,  SKIP,                     "P40-GPIO"  },
    {28,  SKIP,                     "P40-GPIO"  },
    {29,  SKIP,                     "P40-GPIO"  },
    {30,  GCH(3, 6), AdcKind_Pin,     "SPARE"     },  /* reserved, pin level only */
    {31,  GCH(3, 7), AdcKind_Pin,     "SPARE"     },
    {32,  SKIP,                     "P40-GPIO"  },
    {33,  SKIP,                     "P40-GPIO"  },
    {34,  GCH(8, 2), AdcKind_Pin,     "SPARE"     },  /* reserved, pin level only */
    {35,  GCH(8, 3), AdcKind_Direct,  "T1S_INH"   },  /* LAN8651 10BASE-T1S INH */
    {36,  SKIP,                     "P40-GPIO"  },
    {37,  SKIP,                     "P40-GPIO"  },
    {38,  SKIP,                     "P40-GPIO"  },
    {39,  SKIP,                     "P40-GPIO"  },
    {40,  GCH(8, 8), AdcKind_Div50_3, "EXTADC0"   },  /* ext ADC in, 47K+3K */
    {41,  GCH(8, 9), AdcKind_Div50_3, "EXTADC1"   },
    {42,  GCH(8,10), AdcKind_Div50_3, "EXTADC2"   },
    {43,  GCH(8,11), AdcKind_Div50_3, "EXTADC3"   },
    {44,  GCH(8,12), AdcKind_Div50_3, "EXTADC4"   },
    {45,  GCH(8,13), AdcKind_Div50_3, "EXTADC5"   },
    {46,  GCH(8,14), AdcKind_Div50_3, "EXTADC6"   },
    {47,  GCH(8,15), AdcKind_Div50_3, "EXTADC7"   },
};

/* ---- EVADC handles ---- */
static IfxEvadc_Adc       s_evadc;
static IfxEvadc_Adc_Group s_grp0, s_grp1, s_grp2, s_grp3, s_grp8;

/* Channel handles indexed by AN (only valid for sampled ANs) */
static IfxEvadc_Adc_Channel s_ch[48];

static IfxEvadc_Adc_Group *Adc_GroupFor(uint8 grpId)
{
    switch (grpId)
    {
        case 0: return &s_grp0;
        case 1: return &s_grp1;
        case 2: return &s_grp2;
        case 3: return &s_grp3;
        case 8: return &s_grp8;
        default: return 0;
    }
}

static IfxEvadc_GroupId Adc_GroupIdEnum(uint8 grpId)
{
    switch (grpId)
    {
        case 0: return IfxEvadc_GroupId_0;
        case 1: return IfxEvadc_GroupId_1;
        case 2: return IfxEvadc_GroupId_2;
        case 3: return IfxEvadc_GroupId_3;
        case 8: return IfxEvadc_GroupId_8;
        default: return IfxEvadc_GroupId_0;
    }
}

static void Adc_InitGroup(IfxEvadc_Adc_Group *grp, uint8 grpId, boolean last)
{
    IfxEvadc_Adc_GroupConfig grpCfg;

    IfxEvadc_Adc_initGroupConfig(&grpCfg, &s_evadc);
    grpCfg.groupId = Adc_GroupIdEnum(grpId);
    grpCfg.master  = grpCfg.groupId;
    /* free-running queue0: gate always, refill scan */
    grpCfg.queueRequest[0].triggerConfig.gatingMode = IfxEvadc_GatingMode_always;
    grpCfg.arbiter.requestSlotQueue0Enabled = TRUE;
    grpCfg.arbiter.requestSlotQueue1Enabled = FALSE;
    grpCfg.arbiter.requestSlotQueue2Enabled = FALSE;
    if (last)
    {
        grpCfg.startupCalibration = TRUE;
    }
    IfxEvadc_Adc_initGroup(grp, &grpCfg);
}

static void Adc_InitChannelsForGroup(IfxEvadc_Adc_Group *grp)
{
    uint8 an;

    for (an = 0; an < 48; ++an)
    {
        const AdcAnInfo *info = &g_AdcAnTable[an];
        IfxEvadc_Adc_ChannelConfig chCfg;

        if (info->kind == AdcKind_Skip)
        {
            continue;
        }
        if (Adc_GroupFor(info->group) != grp)
        {
            continue;
        }
        IfxEvadc_Adc_initChannelConfig(&chCfg, grp);
        chCfg.channelId     = (IfxEvadc_ChannelId)info->channel;
        chCfg.resultRegister = (IfxEvadc_ChannelResult)info->channel;
        IfxEvadc_Adc_initChannel(&s_ch[an], &chCfg);
        IfxEvadc_Adc_addToQueue(&s_ch[an], IfxEvadc_RequestSource_queue0, IFXEVADC_QUEUE_REFILL);
    }
    IfxEvadc_Adc_startQueue(grp, IfxEvadc_RequestSource_queue0);
}

void Adc_Init(void)
{
    IfxEvadc_Adc_Config modCfg;

    IfxEvadc_Adc_initModuleConfig(&modCfg, &MODULE_EVADC);
    IfxEvadc_Adc_initModule(&s_evadc, &modCfg);

    /* Slave groups first, master(s) last; all independent masters here.
     * Startup calibration on the last configured group (G8). */
    Adc_InitGroup(&s_grp0, 0, FALSE);
    Adc_InitGroup(&s_grp1, 1, FALSE);
    Adc_InitGroup(&s_grp2, 2, FALSE);
    Adc_InitGroup(&s_grp3, 3, FALSE);
    Adc_InitGroup(&s_grp8, 8, TRUE);

    Adc_InitChannelsForGroup(&s_grp0);
    Adc_InitChannelsForGroup(&s_grp1);
    Adc_InitChannelsForGroup(&s_grp2);
    Adc_InitChannelsForGroup(&s_grp3);
    Adc_InitChannelsForGroup(&s_grp8);
}

int Adc_ReadAn(uint8 an, uint16 *raw, float *voltPin)
{
    Ifx_EVADC_G_RES res;
    uint32 timeout;

    if (an >= 48)
    {
        return 0;
    }
    if (g_AdcAnTable[an].kind == AdcKind_Skip)
    {
        return 0;
    }
    /* Background refill scan keeps converting; wait for a fresh valid flag.
     * Retry a few rounds to cover a conversion in progress right at boot. */
    for (uint32 attempt = 0; attempt < 3u; ++attempt)
    {
        timeout = 200000u;
        do
        {
            res = IfxEvadc_Adc_getResult(&s_ch[an]);
            if (res.B.VF)
            {
                break;
            }
        } while (--timeout != 0u);
        if (res.B.VF)
        {
            break;
        }
    }

    if (!res.B.VF)
    {
        return 0;
    }
    if (raw != 0)
    {
        *raw = (uint16)res.B.RESULT;
    }
    if (voltPin != 0)
    {
        *voltPin = Adc_RawToVolt((uint16)res.B.RESULT);
    }
    return 1;
}
