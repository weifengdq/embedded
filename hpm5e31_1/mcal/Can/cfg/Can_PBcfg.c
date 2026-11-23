/*
 * Can_PBcfg.c —— CAN 驱动 post-build 配置实例（契约 §5.3、§3.3）
 *
 * 本文件是"整块板子的 CAN 资源映射表"：MCAN 实例、引脚、外设时钟、收发器 STB、
 * 消息 RAM（HPM5E31 的 M_CAN 消息 RAM 在共享 AHB SRAM）、位时序、过滤器、
 * 中断号与中断向量，全部集中在这里。驱动源码里没有任何板级硬编码。
 *
 * 依据：
 *   - docs/MCAL开发约定.md §2 硬件接线与资源分配（MCAN0~3 全归 Can）
 *   - AUTOSAR_CP_SWS_CANDriver §10 CanConfigSet / CanController /
 *     CanControllerBaudrateConfig / CanHardwareObject / CanFilterMask
 *   - HPM5E31 RM "MCAN" 章；boards/hpm5e31_1/ext/board_can.h（本板 CAN 资源宏）
 *   - 板级实测：CAN0(PD00/PD01) <-> CAN1(PD05/PD04)、CAN2(PC08/PA09) <-> CAN3(PC15/PC14)
 *     两两 120 Ω 对接，收发器 TCAN1044AV 的 STB 由 PE03 统一控制（0=正常）
 */
#include "Can_PBcfg.h"
#include "board.h"
#include "pinmux.h"
#include "hpm_mcan_drv.h"
#include "hpm_soc_irq.h"
#include "hpm_gpio_drv.h"

#define CAN_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "MemMap.h"

/* ========================================================================
 * 1) 消息 RAM：每实例 640 字（2560 B），4 实例共 10 KB（AHB SRAM 共 32 KB）
 *    HPM5E31 的 MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1，必须放在 .ahb_sram 段，
 *    否则 mcan_set_msg_buf_attr() 会以 status_invalid_argument 拒绝。
 * ======================================================================== */
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#define CAN_MSG_RAM_ATTR ATTR_PLACE_AT(".ahb_sram")
#else
#define CAN_MSG_RAM_ATTR
#endif

CAN_MSG_RAM_ATTR static uint32_t s_mcan_msg_ram[CAN_CONTROLLER_COUNT][CAN_MSG_BUF_WORDS];

/* ========================================================================
 * 2) 位时序表（CAN 时钟 = pll1_clk0 / 5 = 80 MHz，TQ = 12.5 ns @presc=1）
 *    所有数值都是"物理值"，驱动负责 -1 编码进 NBTP/DBTP。
 *
 *    ID 0  经典 500 kbit/s  SP 87.5%   (1+139+20 = 160 TQ)
 *    ID 1  经典 1 Mbit/s    SP 75.0%   (presc=2, 1+29+10 = 40 TQ)
 *    ID 2  FD 500 k / 2 M   SP 75.0%   数据段 1+14+5 = 20 TQ @presc=2, TDC 开
 *    ID 3  FD 500 k / 5 M   SP 75.0%   数据段 1+11+4 = 16 TQ @presc=1, TDC 开
 * ======================================================================== */
static const Can_BaudrateConfigItemType s_can_baud_cfgs[CAN_MAX_BAUDRATE_CONFIGS] = {
    /* ---- 0: 经典 CAN 500 kbit/s，采样点 87.5% ---- */
    {
        0U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = FALSE, .BrsEnabled = FALSE,
            .DataBaudrate = 0UL,
            .DataPrescaler = 1U, .DataSeg1 = 1U, .DataSeg2 = 2U, .DataSjw = 1U,
            .TdcOffset = 0U, .TdcFilter = 0U
        }
    },
    /* ---- 1: 经典 CAN 1 Mbit/s，采样点 75% ---- */
    {
        1U,
        {
            .Baudrate = 1000000UL,
            .Prescaler = 2U, .Seg1 = 29U, .Seg2 = 10U, .Sjw = 2U,
            .FdEnabled = FALSE, .BrsEnabled = FALSE,
            .DataBaudrate = 0UL,
            .DataPrescaler = 1U, .DataSeg1 = 1U, .DataSeg2 = 2U, .DataSjw = 1U,
            .TdcOffset = 0U, .TdcFilter = 0U
        }
    },
    /* ---- 2: CAN FD 仲裁 500 k / 数据 2 Mbit/s，BRS 开，TDC(15/15) ---- */
    {
        2U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 2000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 14U, .DataSeg2 = 5U, .DataSjw = 2U,
            .TdcOffset = 15U, .TdcFilter = 15U
        }
    },
    /* ---- 3: CAN FD 仲裁 500 k / 数据 5 Mbit/s，BRS 开，TDC(12/12)
     *  数据段 1+11+4 = 16 tq（80 MHz，BRP=1，tq=12.5 ns，200 ns/位），采样点 75%。
     *  TDC 的 SSP 位置必须落在数据位内：TDCO=20 越过 16 tq 会导致发送端误判位错误，
     *  TEC 累加到 248 后 bus-off（旧配置复现见 logs/20261102-155138-can_fd_echo.log
     *  ID3 行：ok=0/20, errstate=2, TEC=248）。TDCO=12 = 16 tq 的 75%，与采样点一致。
     *  实测 TDCO=8/10/12/13/14 均可通过，推荐 12。 ---- */
    {
        3U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 12U, .TdcFilter = 12U
        }
    },
    /* ---- 4: CAN FD 仲裁 500 k / 数据 1 Mbit/s（BRS 开，数据段 <=1M 不需要 TDC） ---- */
    {
        4U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 1000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 19U, .DataSeg2 = 5U, .DataSjw = 2U,
            .TdcOffset = 0U, .TdcFilter = 0U
        }
    },
    /* ---- 5: CAN FD 仲裁 1 M / 数据 2 Mbit/s（BRS 开，TDC 自动 SSP） ---- */
    {
        5U,
        {
            .Baudrate = 1000000UL,
            .Prescaler = 2U, .Seg1 = 29U, .Seg2 = 10U, .Sjw = 2U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 2000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 14U, .DataSeg2 = 5U, .DataSjw = 2U,
            .TdcOffset = 0U, .TdcFilter = 0U
        }
    },
    /* ---- 6: FD 500k/5M SP75%，TDCO=12（16 tq 数据位的 75%） ---- */
    {
        6U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 12U, .TdcFilter = 12U
        }
    },
    /* ---- 7: FD 500k/5M SP75%，TDCO=8（50%） ---- */
    {
        7U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 8U, .TdcFilter = 8U
        }
    },
    /* ---- 8: FD 500k/5M SP75%，TDCO=10（62.5%） ---- */
    {
        8U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 10U, .TdcFilter = 10U
        }
    },
    /* ---- 9: FD 500k/5M SP75%，TDCO=13（81%） ---- */
    {
        9U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 13U, .TdcFilter = 13U
        }
    },
    /* ---- 10: FD 500k/5M SP75%，TDCO=14（87.5%） ---- */
    {
        10U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 14U, .TdcFilter = 14U
        }
    },
    /* ---- 11: FD 500k/5M SP80%，TDCO=12（seg1=12 seg2=3） ---- */
    {
        11U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 12U, .DataSeg2 = 3U, .DataSjw = 2U,
            .TdcOffset = 12U, .TdcFilter = 12U
        }
    },
    /* ---- 12: FD 500k/5M SP87.5%，TDCO=13（seg1=13 seg2=2） ---- */
    {
        12U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 13U, .DataSeg2 = 2U, .DataSjw = 2U,
            .TdcOffset = 13U, .TdcFilter = 13U
        }
    },
    /* ---- 13: FD 仲裁1M/数据5M SP75%，TDCO=12 ---- */
    {
        13U,
        {
            .Baudrate = 1000000UL,
            .Prescaler = 2U, .Seg1 = 29U, .Seg2 = 10U, .Sjw = 2U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 12U, .TdcFilter = 12U
        }
    },
    /* ---- 14: FD 500k/2M TDCO=1（测收发器环回延迟 TDCV-1） ---- */
    {
        14U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 2000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 14U, .DataSeg2 = 5U, .DataSjw = 2U,
            .TdcOffset = 1U, .TdcFilter = 1U
        }
    },
    /* ---- 15: FD 500k/5M TDCO=1（测 5M 下发器环回延迟） ---- */
    {
        15U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 1U, .TdcFilter = 1U
        }
    },
    /* ---- 16: FD 500k/2M SP80%，TDCO=16（seg1=15 seg2=4） ---- */
    {
        16U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 2000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 15U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 16U, .TdcFilter = 16U
        }
    },
    /* ---- 17: FD 500k/2M SP85%，TDCO=17（seg1=16 seg2=3） ---- */
    {
        17U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 2000000UL,
            .DataPrescaler = 2U, .DataSeg1 = 16U, .DataSeg2 = 3U, .DataSjw = 2U,
            .TdcOffset = 17U, .TdcFilter = 17U
        }
    }
    ,
    /* ---- 18: FD 500k/5M，TDCO=20（>16 tq 数据位，非法；仅用于验证驱动拒绝非法 TDC） ---- */
    {
        18U,
        {
            .Baudrate = 500000UL,
            .Prescaler = 1U, .Seg1 = 139U, .Seg2 = 20U, .Sjw = 4U,
            .FdEnabled = TRUE, .BrsEnabled = TRUE,
            .DataBaudrate = 5000000UL,
            .DataPrescaler = 1U, .DataSeg1 = 11U, .DataSeg2 = 4U, .DataSjw = 2U,
            .TdcOffset = 20U, .TdcFilter = 20U
        }
    }
};

/* ========================================================================
 * 3) 板级动作：引脚复用 / 外设时钟 / 收发器 STB
 *    统一走 boards/hpm5e31_1 的 board_* API，避免在 MCAL 里复制 pinmux 细节
 * ======================================================================== */
static void can_pin_init_0(void) { board_init_can(BOARD_CAN0_BASE); }
static void can_pin_init_1(void) { board_init_can(BOARD_CAN1_BASE); }
static void can_pin_init_2(void) { board_init_can(BOARD_CAN2_BASE); }
static void can_pin_init_3(void) { board_init_can(BOARD_CAN3_BASE); }

static uint32 can_clock_init_0(void) { return board_init_can_clock(BOARD_CAN0_BASE); }
static uint32 can_clock_init_1(void) { return board_init_can_clock(BOARD_CAN1_BASE); }
static uint32 can_clock_init_2(void) { return board_init_can_clock(BOARD_CAN2_BASE); }
static uint32 can_clock_init_3(void) { return board_init_can_clock(BOARD_CAN3_BASE); }

/*! 4 颗 TCAN1044AV 共用 PE03 作为 STB（低电平 = 正常工作） */
static void can_transceiver_enable(void)
{
    board_can_transceivers_enable();
}

/* ========================================================================
 * 4) 控制器表：4 路 MCAN
 * ======================================================================== */
static const Can_ControllerConfigType s_can_controllers[CAN_CONTROLLER_COUNT] = {
    {
        .ControllerId = 0U,
        .BaseAddress = (void *) BOARD_CAN0_BASE,
        .IrqNumber = (uint32) IRQn_MCAN0,
        .MsgBufAddress = (uint32) (uintptr_t) &s_mcan_msg_ram[0][0],
        .MsgBufSize = (uint32) sizeof(s_mcan_msg_ram[0]),
        .DedicatedTxBufCount = 4U, .TxFifoQueueCount = 4U,
        .RxFifo0Count = 8U, .RxFifo1Count = 4U,
        .RxFifo0Watermark = 0U, .RxFifo1Watermark = 0U,
        .StdFilterCount = 6U, .ExtFilterCount = 3U,
        .TxEvtFifoCount = 0U, .TxEvtFifoWatermark = 0U,
        .AcceptNonMatchingStd = 2U, /* 2 = 丢弃未命中帧 */
        .AcceptNonMatchingExt = 2U,
        .RejectRemoteStd = TRUE, .RejectRemoteExt = TRUE,
        .EnableEdgeFiltering = FALSE,
        .UseTimestampCounter = TRUE,
        .BaudrateConfigs = s_can_baud_cfgs,
        .BaudrateConfigCount = CAN_MAX_BAUDRATE_CONFIGS,
        .DefaultBaudrateConfigId = 0U,
        .PinInit = can_pin_init_0,
        .ClockInit = can_clock_init_0,
        .TransceiverEnable = can_transceiver_enable
    },
    {
        .ControllerId = 1U,
        .BaseAddress = (void *) BOARD_CAN1_BASE,
        .IrqNumber = (uint32) IRQn_MCAN1,
        .MsgBufAddress = (uint32) (uintptr_t) &s_mcan_msg_ram[1][0],
        .MsgBufSize = (uint32) sizeof(s_mcan_msg_ram[1]),
        .DedicatedTxBufCount = 4U, .TxFifoQueueCount = 4U,
        .RxFifo0Count = 8U, .RxFifo1Count = 4U,
        .RxFifo0Watermark = 0U, .RxFifo1Watermark = 0U,
        .StdFilterCount = 6U, .ExtFilterCount = 3U,
        .TxEvtFifoCount = 0U, .TxEvtFifoWatermark = 0U,
        .AcceptNonMatchingStd = 2U,
        .AcceptNonMatchingExt = 2U,
        .RejectRemoteStd = TRUE, .RejectRemoteExt = TRUE,
        .EnableEdgeFiltering = FALSE,
        .UseTimestampCounter = TRUE,
        .BaudrateConfigs = s_can_baud_cfgs,
        .BaudrateConfigCount = CAN_MAX_BAUDRATE_CONFIGS,
        .DefaultBaudrateConfigId = 0U,
        .PinInit = can_pin_init_1,
        .ClockInit = can_clock_init_1,
        .TransceiverEnable = can_transceiver_enable
    },
    {
        .ControllerId = 2U,
        .BaseAddress = (void *) BOARD_CAN2_BASE,
        .IrqNumber = (uint32) IRQn_MCAN2,
        .MsgBufAddress = (uint32) (uintptr_t) &s_mcan_msg_ram[2][0],
        .MsgBufSize = (uint32) sizeof(s_mcan_msg_ram[2]),
        .DedicatedTxBufCount = 4U, .TxFifoQueueCount = 4U,
        .RxFifo0Count = 8U, .RxFifo1Count = 4U,
        .RxFifo0Watermark = 0U, .RxFifo1Watermark = 0U,
        .StdFilterCount = 6U, .ExtFilterCount = 3U,
        .TxEvtFifoCount = 0U, .TxEvtFifoWatermark = 0U,
        .AcceptNonMatchingStd = 2U,
        .AcceptNonMatchingExt = 2U,
        .RejectRemoteStd = TRUE, .RejectRemoteExt = TRUE,
        .EnableEdgeFiltering = FALSE,
        .UseTimestampCounter = TRUE,
        .BaudrateConfigs = s_can_baud_cfgs,
        .BaudrateConfigCount = CAN_MAX_BAUDRATE_CONFIGS,
        .DefaultBaudrateConfigId = 0U,
        .PinInit = can_pin_init_2,
        .ClockInit = can_clock_init_2,
        .TransceiverEnable = can_transceiver_enable
    },
    {
        .ControllerId = 3U,
        .BaseAddress = (void *) BOARD_CAN3_BASE,
        .IrqNumber = (uint32) IRQn_MCAN3,
        .MsgBufAddress = (uint32) (uintptr_t) &s_mcan_msg_ram[3][0],
        .MsgBufSize = (uint32) sizeof(s_mcan_msg_ram[3]),
        .DedicatedTxBufCount = 4U, .TxFifoQueueCount = 4U,
        .RxFifo0Count = 8U, .RxFifo1Count = 4U,
        .RxFifo0Watermark = 0U, .RxFifo1Watermark = 0U,
        .StdFilterCount = 6U, .ExtFilterCount = 3U,
        .TxEvtFifoCount = 0U, .TxEvtFifoWatermark = 0U,
        .AcceptNonMatchingStd = 2U,
        .AcceptNonMatchingExt = 2U,
        .RejectRemoteStd = TRUE, .RejectRemoteExt = TRUE,
        .EnableEdgeFiltering = FALSE,
        .UseTimestampCounter = TRUE,
        .BaudrateConfigs = s_can_baud_cfgs,
        .BaudrateConfigCount = CAN_MAX_BAUDRATE_CONFIGS,
        .DefaultBaudrateConfigId = 0U,
        .PinInit = can_pin_init_3,
        .ClockInit = can_clock_init_3,
        .TransceiverEnable = can_transceiver_enable
    }
};

/* ========================================================================
 * 5) HTH：每控制器 4 个专用发送缓冲，映射到 TX buffer 0..3
 * ======================================================================== */
static const Can_HthConfigType s_can_hths[CAN_MAX_HTH] = {
    { 0U, 0U }, { 0U, 1U }, { 0U, 2U }, { 0U, 3U },
    { 1U, 0U }, { 1U, 1U }, { 1U, 2U }, { 1U, 3U },
    { 2U, 0U }, { 2U, 1U }, { 2U, 2U }, { 2U, 3U },
    { 3U, 0U }, { 3U, 1U }, { 3U, 2U }, { 3U, 3U }
};

/* ========================================================================
 * 6) HRH：每控制器 2 个（RXFIFO0 / RXFIFO1）
 * ======================================================================== */
static const Can_HrhConfigType s_can_hrhs[CAN_MAX_HRH] = {
    { 0U, 0U }, { 0U, 1U },
    { 1U, 0U }, { 1U, 1U },
    { 2U, 0U }, { 2U, 1U },
    { 3U, 0U }, { 3U, 1U }
};

/* ========================================================================
 * 7) 过滤器表（每控制器一套，4 套相同）
 *
 *    标准 ID 列表：
 *      #0 掩码  0x123 / 0x7FF   -> FIFO0   （经典/FD 回环 REQ）
 *      #1 掩码  0x321 / 0x7FF   -> FIFO0   （回环 ECHO）
 *      #2 范围  0x400 .. 0x40F  -> FIFO0   （范围过滤测试）
 *      #3 双 ID {0x500,0x501}   -> FIFO1   （双 ID 过滤测试）
 *      #4 掩码  0x600 / 0x7F0   -> FIFO0   （0x600~0x60F 掩码测试）
 *      #5 范围  0x700 .. 0x7FF  -> 丢弃     （拒绝过滤测试）
 *    扩展 ID 列表：
 *      #0 范围  0x18000000 .. 0x18FFFFFF -> FIFO0
 *      #1 掩码  0x1ABC0000 / 0x1FFF0000  -> FIFO0
 *      #2 双 ID {0x1FFFFFF0,0x1FFFFFF1}  -> FIFO1
 *    未命中全局过滤器配置 -> 丢弃（AcceptNonMatching* = 2）。
 * ======================================================================== */
#define CAN_FILTER_SET(c) \
    { (c), CAN_FILTER_LIST_STD, 0U, CAN_FILTER_TYPE_MASK,  CAN_FILTER_STORE_FIFO0, 0x123U, 0x7FFU }, \
    { (c), CAN_FILTER_LIST_STD, 1U, CAN_FILTER_TYPE_MASK,  CAN_FILTER_STORE_FIFO0, 0x321U, 0x7FFU }, \
    { (c), CAN_FILTER_LIST_STD, 2U, CAN_FILTER_TYPE_RANGE, CAN_FILTER_STORE_FIFO0, 0x400U, 0x40FU }, \
    { (c), CAN_FILTER_LIST_STD, 3U, CAN_FILTER_TYPE_DUAL,  CAN_FILTER_STORE_FIFO1, 0x500U, 0x501U }, \
    { (c), CAN_FILTER_LIST_STD, 4U, CAN_FILTER_TYPE_MASK,  CAN_FILTER_STORE_FIFO0, 0x600U, 0x7F0U }, \
    { (c), CAN_FILTER_LIST_STD, 5U, CAN_FILTER_TYPE_RANGE, CAN_FILTER_STORE_REJECT, 0x700U, 0x7FFU }, \
    { (c), CAN_FILTER_LIST_EXT, 0U, CAN_FILTER_TYPE_RANGE, CAN_FILTER_STORE_FIFO0, 0x18000000UL, 0x18FFFFFFUL }, \
    { (c), CAN_FILTER_LIST_EXT, 1U, CAN_FILTER_TYPE_MASK,  CAN_FILTER_STORE_FIFO0, 0x1ABC0000UL, 0x1FFF0000UL }, \
    { (c), CAN_FILTER_LIST_EXT, 2U, CAN_FILTER_TYPE_DUAL,  CAN_FILTER_STORE_FIFO1, 0x1FFFFFF0UL, 0x1FFFFFF1UL }

static const Can_FilterConfigType s_can_filters[] = {
    CAN_FILTER_SET(0U),
    CAN_FILTER_SET(1U),
    CAN_FILTER_SET(2U),
    CAN_FILTER_SET(3U)
};

/* ========================================================================
 * 8) 总配置实例
 * ======================================================================== */
const Can_ConfigType Can_Config = {
    .Controllers = s_can_controllers,
    .ControllerCount = CAN_CONTROLLER_COUNT,
    .Hths = s_can_hths,
    .HthCount = CAN_MAX_HTH,
    .Hrhs = s_can_hrhs,
    .HrhCount = CAN_MAX_HRH,
    .Filters = s_can_filters,
    .FilterCount = (uint8) (sizeof(s_can_filters) / sizeof(s_can_filters[0]))
};

/* ========================================================================
 * 9) 中断向量：M_CAN IRQ 是 RV32 机器模式外部中断，向量表槽位在链接期绑定，
 *    所以"中断号 -> 中断入口"的映射必须写在本文件（板级），而不是驱动里。
 * ======================================================================== */
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN0, Can_Isr_0)
void Can_Isr_0(void)
{
    Can_Isr_Handler(0U);
}

SDK_DECLARE_EXT_ISR_M(IRQn_MCAN1, Can_Isr_1)
void Can_Isr_1(void)
{
    Can_Isr_Handler(1U);
}

SDK_DECLARE_EXT_ISR_M(IRQn_MCAN2, Can_Isr_2)
void Can_Isr_2(void)
{
    Can_Isr_Handler(2U);
}

SDK_DECLARE_EXT_ISR_M(IRQn_MCAN3, Can_Isr_3)
void Can_Isr_3(void)
{
    Can_Isr_Handler(3U);
}

#define CAN_STOP_SEC_CONFIG_DATA_UNSPECIFIED
#include "MemMap.h"
