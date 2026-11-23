/*
 * Can.c —— AUTOSAR CAN Driver（模块 ID 80）实现，HPM5E31 4 路 Bosch M_CAN
 *
 * 依据：
 *   - AUTOSAR_CP_SWS_CANDriver（R25-11），本地文本
 *     autosar_txt/AUTOSAR_CP_SWS_CANDriver.txt
 *     关键条目：SWS_Can_00223(Init) / 00174,00408(Init 状态检查) / 00230(SetControllerMode)
 *     00233(Write) / 00212-00214(Write 互斥与 CAN_BUSY) / 00491-00494,00256,00260(SetBaudrate)
 *     00384(STARTED 重初始化) / 91003,91013,91021(枚举) / 2268-2276(DET 错误码)
 *   - HPM5E31 RM 第 "CAN" / "MCAN" 章：M_CAN 寄存器 CCCR/PSR/ECR/NBTP/DBTP/SIDFC/XIDFC/
 *     RXF0C/RXF1C/TXBC/TXEFC/IR/IE/ILE 语义见 hpm_mcan_regs.h
 *   - Bosch M_CAN User Manual（M_CAN 控制器寄存器语义的原始出处）
 *
 * 实现取舍（第 47 章 §对比详述）：
 *   1. 位时序/消息 RAM/过滤器布局直接写 M_CAN 寄存器（经 SDK 的寄存器级 inline 助手），
 *      不调用 SDK 的 mcan_transmit_blocking 等阻塞式高层 API；
 *   2. 发送走"专用 TX Buffer"（一个 HTH 一个 buffer），确认由 TXBTO 位判定，
 *      与 AUTOSAR HTH 语义一一对应；
 *   3. 接收走 RXFIFO0/1，由 IRQ 驱动（CCC 的 CAN_RX_PROCESSING_INTERRUPT 可切轮询）；
 *   4. 板级实例/引脚/时钟/中断号全部来自 Can_PBcfg.c，本文件零硬编码（契约 §3.3）。
 */
#include "Can.h"
#include "Det.h"
#include "MemMap.h"

#include "hpm_common.h"
#include "hpm_mcan_drv.h"
#include "hpm_mcan_regs.h"
#include "hpm_interrupt.h"

/* ========================================================================
 * 内部宏
 * ======================================================================== */
#define CAN_DLC_BYTES_TABLE_INIT {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U}

/*! 中断使能掩码：接收新报文 + 错误状态 + 发送完成 */
#define CAN_IRQ_RX_MASK   (MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO1_NEW_MSG)
#define CAN_IRQ_ERR_MASK  (MCAN_INT_BUS_OFF_STATUS | MCAN_INT_WARNING_STATUS | \
                           MCAN_INT_ERROR_PASSIVE | MCAN_INT_PROTOCOL_ERR_IN_ARB_PHASE | \
                           MCAN_INT_PROTOCOL_ERR_IN_DATA_PHASE | MCAN_INT_BIT_ERROR_UNCORRECTED)
#define CAN_IRQ_LOST_MASK (MCAN_INT_RXFIFO0_MSG_LOST | MCAN_INT_RXFIFO1_MSG_LOST)

/* ========================================================================
 * 模块内部状态
 * ======================================================================== */
#define CAN_START_SEC_VAR_CLEARED_UNSPECIFIED
#include "MemMap.h"

static const Can_ConfigType *s_can_config;                          /*!< NULL = 未初始化 */
static Can_ControllerStateType s_ctrl_state[CAN_CONTROLLER_COUNT];  /*!< 控制器状态机 */
static uint16 s_ctrl_baud_id[CAN_CONTROLLER_COUNT];                 /*!< 当前波特率配置 ID */
static uint8  s_ctrl_int_disable_cnt[CAN_CONTROLLER_COUNT];         /*!< 关中断嵌套计数 */
static uint32 s_ctrl_int_mask[CAN_CONTROLLER_COUNT];               /*!< 本控制器配置的中断掩码 */
static boolean s_busoff_reported[CAN_CONTROLLER_COUNT];             /*!< bus-off 是否已上报 */
static boolean s_errpassive_reported[CAN_CONTROLLER_COUNT];         /*!< error passive 是否已上报 */
static boolean s_hth_busy[CAN_MAX_HTH];                             /*!< HTH 互斥标记 */
static PduIdType s_hth_pdu[CAN_MAX_HTH];                            /*!< HTH 上的 swPduHandle */

/* 过滤器装载用的临时表（mcan_init 内同步使用，非重入保护由 Can_Init 的 Non-Reentrant 保证） */
static mcan_filter_elem_t s_filter_std_buf[CAN_MAX_FILTER_ELEMS];
static mcan_filter_elem_t s_filter_ext_buf[CAN_MAX_FILTER_ELEMS];

#define CAN_STOP_SEC_VAR_CLEARED_UNSPECIFIED
#include "MemMap.h"

/* ========================================================================
 * 内部工具函数
 * ======================================================================== */
#define CAN_START_SEC_CODE
#include "MemMap.h"

static MCAN_Type *can_base(uint8 ctrl)
{
    return (MCAN_Type *) s_can_config->Controllers[ctrl].BaseAddress;
}

/*! 在控制器的波特率配置表里按 ID 查表；找不到返回 NULL */
static const Can_BaudrateConfigItemType *can_find_baud_cfg(uint8 ctrl, uint16 id)
{
    const Can_ControllerConfigType *cc = &s_can_config->Controllers[ctrl];
    uint8 i;
    for (i = 0U; i < cc->BaudrateConfigCount; i++) {
        if (cc->BaudrateConfigs[i].ConfigId == id) {
            return &cc->BaudrateConfigs[i];
        }
    }
    return NULL_PTR;
}

/*! 字节长度 -> DLC。CAN FD 的 9..15 对应 12/16/20/24/32/48/64 字节 */
static uint8 can_dlc_from_len(uint8 len)
{
    static const uint8 dlc_bytes[16] = CAN_DLC_BYTES_TABLE_INIT;
    uint8 dlc;
    if (len <= 8U) {
        return len;
    }
    for (dlc = 9U; dlc < 16U; dlc++) {
        if (len <= dlc_bytes[dlc]) {
            return dlc;
        }
    }
    return 15U; /* > 64 由调用方先用 DET 拦掉 */
}

/*! DLC -> 字节长度 */
static uint8 can_len_from_dlc(uint8 dlc)
{
    static const uint8 dlc_bytes[16] = CAN_DLC_BYTES_TABLE_INIT;
    return dlc_bytes[dlc & 0x0FU];
}

/*! 轮询等待 CCCR.INIT 达到期望值（写 CCCR 是 posted write，必须轮询确认） */
static Std_ReturnType can_wait_cccr_init(MCAN_Type *base, boolean expect_init)
{
    uint32 guard = 10000U;
    uint32 want = (expect_init != FALSE) ? MCAN_CCCR_INIT_MASK : 0U;

    while (((base->CCCR & MCAN_CCCR_INIT_MASK) != want) && (guard > 0U)) {
        guard--;
    }
    return ((base->CCCR & MCAN_CCCR_INIT_MASK) == want) ? E_OK : E_NOT_OK;
}

/*! 按控制器配置算中断掩码 */
static uint32 can_int_mask_of(const Can_ControllerConfigType *cc)
{
    /* 错误类中断始终打开（bus-off / error passive 是异步事件，只能靠中断捕获） */
    uint32 mask = CAN_IRQ_ERR_MASK;

#if (CAN_RX_PROCESSING_INTERRUPT == 1U)
    mask |= CAN_IRQ_RX_MASK | CAN_IRQ_LOST_MASK;
    if (cc->RxFifo0Count == 0U) {
        mask &= ~MCAN_INT_RXFIFO0_NEW_MSG;
    }
    if (cc->RxFifo1Count == 0U) {
        mask &= ~MCAN_INT_RXFIFO1_NEW_MSG;
    }
#else
    (void) cc;
#endif
#if (CAN_TX_CONFIRM_IN_INTERRUPT == 1U)
    mask |= MCAN_INT_TX_COMPLETED;
#endif
    return mask;
}

/*! 组装消息 RAM 布局 + 位时序 + 过滤器 + 中断，并初始化一个控制器 */
static Std_ReturnType can_init_controller(uint8 ctrl, uint16 baud_id)
{
    const Can_ControllerConfigType *cc = &s_can_config->Controllers[ctrl];
    MCAN_Type *base = (MCAN_Type *) cc->BaseAddress;
    const Can_BaudrateConfigItemType *bc = can_find_baud_cfg(ctrl, baud_id);
    mcan_config_t mcfg;
    uint32 clk_hz;
    uint32 std_cnt = 0U;
    uint32 ext_cnt = 0U;
    uint8 i;

    if ((base == NULL_PTR) || (bc == NULL_PTR)) {
        return E_NOT_OK;
    }
    if ((bc->Timing.Baudrate == 0U) || (bc->Timing.Prescaler == 0U)) {
        return E_NOT_OK;
    }
    /*
     * CAN FD 发送延迟补偿（TDC）的 SSP 位置必须落在数据段位时间内：
     *   SSP = TDCR.TDCO（单位 = 数据段 mtq），数据位长度 = 1 + DTSEG1 + DTSEG2。
     * 实测 500k/5M、16 tq 数据位：TDCO=8..14 全部无错，TDCO=20（>16）时发送端在数据段
     * 误判位错误，TEC 一路涨到 248 进 bus-off（logs/20261102-155138-can_fd_echo.log）。
     * 这里做配置期检查，避免非法 TDC 值被静默编进固件。TdcOffset=0 表示不启用 TDC。
     */
    if ((bc->Timing.FdEnabled != FALSE) && (bc->Timing.TdcOffset != 0U)) {
        uint32 data_bit_tq = 1U + (uint32)bc->Timing.DataSeg1 + (uint32)bc->Timing.DataSeg2;
        if ((uint32)bc->Timing.TdcOffset >= data_bit_tq) {
            return E_NOT_OK;
        }
        if (bc->Timing.TdcFilter >= data_bit_tq) {
            return E_NOT_OK;
        }
    }

    /* 1) 板级：引脚复用 -> 外设时钟 -> 收发器 STB */
    if (cc->PinInit != NULL_PTR) {
        cc->PinInit();
    }
    clk_hz = (cc->ClockInit != NULL_PTR) ? cc->ClockInit() : 0U;
    if (cc->TransceiverEnable != NULL_PTR) {
        cc->TransceiverEnable();
    }
    if (clk_hz == 0U) {
        return E_NOT_OK;
    }

    /* 2) 消息 RAM（HPM5E31 的 M_CAN 消息 RAM 在共享 AHB SRAM，必须先登记窗口） */
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr;
        attr.ram_base = cc->MsgBufAddress;
        attr.ram_size = cc->MsgBufSize;
        if (mcan_set_msg_buf_attr(base, &attr) != status_success) {
            return E_NOT_OK;
        }
    }
#endif

    /* 3) 位时序 + FD 参数 */
    mcan_get_default_config(base, &mcfg);
    mcfg.mode = mcan_mode_normal;
    mcfg.enable_canfd = bc->Timing.FdEnabled;
    mcfg.use_lowlevel_timing_setting = true;
    mcfg.can_timing.prescaler = bc->Timing.Prescaler;
    mcfg.can_timing.num_seg1 = bc->Timing.Seg1;
    mcfg.can_timing.num_seg2 = bc->Timing.Seg2;
    mcfg.can_timing.num_sjw = (uint8_t) bc->Timing.Sjw;
    mcfg.can_timing.enable_tdc = false;
    if (bc->Timing.FdEnabled) {
        mcfg.canfd_timing.prescaler = bc->Timing.DataPrescaler;
        mcfg.canfd_timing.num_seg1 = bc->Timing.DataSeg1;
        mcfg.canfd_timing.num_seg2 = bc->Timing.DataSeg2;
        mcfg.canfd_timing.num_sjw = (uint8_t) bc->Timing.DataSjw;
        mcfg.enable_tdc = (bc->Timing.TdcOffset != 0U);
        mcfg.canfd_timing.enable_tdc = mcfg.enable_tdc;
        mcfg.tdc_config.ssp_offset = bc->Timing.TdcOffset;
        mcfg.tdc_config.filter_window_length = bc->Timing.TdcFilter;
    } else {
        mcfg.enable_tdc = false;
    }
    mcfg.enable_edge_filtering = (cc->EnableEdgeFiltering != FALSE) ? true : false;
    mcfg.disable_auto_retransmission = false; /* 保持自动重传：AUTOSAR 默认行为 */
    mcfg.enable_transmit_pause = false;
    mcfg.enable_non_iso_mode = false;
    mcfg.timestamp_cfg.timestamp_selection = (cc->UseTimestampCounter != FALSE)
                                             ? MCAN_TIMESTAMP_SEL_VALUE_INCREMENT
                                             : MCAN_TIMESTAMP_SEL_VALUE_ALWAYS_ZERO;
    mcfg.timestamp_cfg.counter_prescaler = 1U;
    mcfg.use_timestamping_unit = false;

    /* 4) 消息 RAM 分区（RXFIFO/TX Buffer/过滤器/事件 FIFO） */
    mcfg.ram_config.enable_std_filter = (cc->StdFilterCount != 0U);
    mcfg.ram_config.std_filter_elem_count = cc->StdFilterCount;
    mcfg.ram_config.enable_ext_filter = (cc->ExtFilterCount != 0U);
    mcfg.ram_config.ext_filter_elem_count = cc->ExtFilterCount;

    mcfg.ram_config.rxfifos[0].enable = (cc->RxFifo0Count != 0U) ? 1U : 0U;
    mcfg.ram_config.rxfifos[0].elem_count = cc->RxFifo0Count;
    mcfg.ram_config.rxfifos[0].watermark = cc->RxFifo0Watermark;
    mcfg.ram_config.rxfifos[0].operation_mode = MCAN_FIFO_OPERATION_MODE_BLOCKING;
    mcfg.ram_config.rxfifos[0].data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;

    mcfg.ram_config.rxfifos[1].enable = (cc->RxFifo1Count != 0U) ? 1U : 0U;
    mcfg.ram_config.rxfifos[1].elem_count = cc->RxFifo1Count;
    mcfg.ram_config.rxfifos[1].watermark = cc->RxFifo1Watermark;
    mcfg.ram_config.rxfifos[1].operation_mode = MCAN_FIFO_OPERATION_MODE_BLOCKING;
    mcfg.ram_config.rxfifos[1].data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;

    mcfg.ram_config.enable_rxbuf = false;
    mcfg.ram_config.rxbuf_elem_count = 0U;
    mcfg.ram_config.rxbuf_data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;

    mcfg.ram_config.enable_txbuf = true;
    mcfg.ram_config.txbuf_data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;
    mcfg.ram_config.txbuf_dedicated_txbuf_elem_count = cc->DedicatedTxBufCount;
    mcfg.ram_config.txbuf_fifo_or_queue_elem_count = cc->TxFifoQueueCount;
    mcfg.ram_config.txfifo_or_txqueue_mode = MCAN_TXBUF_OPERATION_MODE_FIFO;

    mcfg.ram_config.enable_tx_evt_fifo = (cc->TxEvtFifoCount != 0U);
    mcfg.ram_config.tx_evt_fifo_elem_count = cc->TxEvtFifoCount;
    mcfg.ram_config.tx_evt_fifo_watermark = cc->TxEvtFifoWatermark;

    /* 5) 过滤器列表：按 PBcfg 的 Can_FilterConfigType 表逐元素填，空位保持 disabled */
    for (i = 0U; i < CAN_MAX_FILTER_ELEMS; i++) {
        __builtin_memset(&s_filter_std_buf[i], 0, sizeof(s_filter_std_buf[i]));
        __builtin_memset(&s_filter_ext_buf[i], 0, sizeof(s_filter_ext_buf[i]));
        s_filter_std_buf[i].filter_type = MCAN_FILTER_TYPE_FILTER_DISABLED;
        s_filter_std_buf[i].filter_config = MCAN_FILTER_ELEM_CFG_DISABLED;
        s_filter_std_buf[i].can_id_type = MCAN_CAN_ID_TYPE_STANDARD;
        s_filter_ext_buf[i].filter_type = MCAN_FILTER_TYPE_FILTER_DISABLED;
        s_filter_ext_buf[i].filter_config = MCAN_FILTER_ELEM_CFG_DISABLED;
        s_filter_ext_buf[i].can_id_type = MCAN_CAN_ID_TYPE_EXTENDED;
    }
    for (i = 0U; i < s_can_config->FilterCount; i++) {
        const Can_FilterConfigType *f = &s_can_config->Filters[i];
        mcan_filter_elem_t *elem;
        uint8 type;
        uint8 cfgel;

        if (f->ControllerId != ctrl) {
            continue;
        }
        if (f->ElementIndex >= CAN_MAX_FILTER_ELEMS) {
            continue;
        }
        if (f->FilterList == CAN_FILTER_LIST_EXT) {
            elem = &s_filter_ext_buf[f->ElementIndex];
            if (ext_cnt < (uint32) f->ElementIndex + 1U) {
                ext_cnt = (uint32) f->ElementIndex + 1U;
            }
        } else {
            elem = &s_filter_std_buf[f->ElementIndex];
            if (std_cnt < (uint32) f->ElementIndex + 1U) {
                std_cnt = (uint32) f->ElementIndex + 1U;
            }
        }
        switch (f->StoreTarget) {
        case CAN_FILTER_STORE_FIFO1:
            cfgel = MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO1_IF_MATCH;
            break;
        case CAN_FILTER_STORE_REJECT:
            cfgel = MCAN_FILTER_ELEM_CFG_REJECT_ID_IF_MATCH;
            break;
        case CAN_FILTER_STORE_FIFO0:
        default:
            cfgel = MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH;
            break;
        }
        elem->filter_config = cfgel;
        elem->sync_message = 0U;
        switch (f->FilterType) {
        case CAN_FILTER_TYPE_DUAL:
            type = MCAN_FILTER_TYPE_SPECIFIED_ID_FILTER;
            elem->filter_type = type;
            elem->id1 = f->Id1;
            elem->id2 = f->Id2;
            break;
        case CAN_FILTER_TYPE_MASK:
            type = MCAN_FILTER_TYPE_CLASSIC_FILTER;
            elem->filter_type = type;
            elem->filter_id = f->Id1;
            elem->filter_mask = f->Id2;
            break;
        case CAN_FILTER_TYPE_DISABLED:
            elem->filter_type = MCAN_FILTER_TYPE_FILTER_DISABLED;
            elem->filter_config = MCAN_FILTER_ELEM_CFG_DISABLED;
            break;
        case CAN_FILTER_TYPE_RANGE:
        default:
            type = MCAN_FILTER_TYPE_RANGE_FILTER;
            elem->filter_type = type;
            elem->start_id = f->Id1;
            elem->end_id = f->Id2;
            break;
        }
    }
    mcfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option = cc->AcceptNonMatchingStd;
    mcfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option = cc->AcceptNonMatchingExt;
    mcfg.all_filters_config.global_filter_config.reject_remote_std_frame = (cc->RejectRemoteStd != FALSE) ? true : false;
    mcfg.all_filters_config.global_filter_config.reject_remote_ext_frame = (cc->RejectRemoteExt != FALSE) ? true : false;
    mcfg.all_filters_config.ext_id_mask = 0x1FFFFFFFU;
    mcfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = std_cnt;
    mcfg.all_filters_config.std_id_filter_list.filter_elem_list = (std_cnt > 0U) ? s_filter_std_buf : NULL;
    mcfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = ext_cnt;
    mcfg.all_filters_config.ext_id_filter_list.filter_elem_list = (ext_cnt > 0U) ? s_filter_ext_buf : NULL;

    /* 6) 中断 + 超时看门狗（不用） */
    mcfg.interrupt_mask = can_int_mask_of(cc);
    mcfg.txbuf_trans_interrupt_mask = 0U;
    mcfg.txbuf_cancel_finish_interrupt_mask = 0U;
    mcfg.timeout_cfg.enable_timeout_counter = false;
    mcfg.timeout_cfg.timeout_sel = mcan_timeout_continuous_operation;
    mcfg.timeout_cfg.timeout_period = 0U;

    /* 7) 复位控制器 -> 按上面配置初始化 */
    mcan_deinit(base);
    if (mcan_init(base, &mcfg, clk_hz) != status_success) {
        return E_NOT_OK;
    }

    /* 8) AUTOSAR：Can_Init 之后控制器必须处于 STOPPED（不参与总线）→ 回到 INIT 模式 */
    mcan_enter_init_mode(base);
    if (can_wait_cccr_init(base, true) != E_OK) {
        return E_NOT_OK;
    }
    mcan_clear_interrupt_flags(base, 0xFFFFFFFFU);

    /* 8b) 重新初始化后本控制器的发送对象全部回到空闲（bus-off/重配置后必须清） */
    {
        uint8 h;
        for (h = 0U; h < s_can_config->HthCount; h++) {
            if (s_can_config->Hths[h].ControllerId == ctrl) {
                s_hth_busy[h] = FALSE;
            }
        }
    }

    /* 9) 使能中断控制器（向量由 Can_PBcfg.c 用 SDK_DECLARE_EXT_ISR_M 绑定） */
    s_ctrl_int_mask[ctrl] = mcfg.interrupt_mask;
    s_ctrl_int_disable_cnt[ctrl] = 0U;
    s_ctrl_baud_id[ctrl] = baud_id;
    s_busoff_reported[ctrl] = FALSE;
    s_errpassive_reported[ctrl] = FALSE;
    intc_m_enable_irq_with_priority((uint32_t) cc->IrqNumber, CAN_IRQ_PRIORITY);

    return E_OK;
}

/*! 把一个收到的 M_CAN 报文转成 CanIf_RxIndication 的参数并回调 */
static void can_rx_indicate(uint8 ctrl, uint8 fifo, const mcan_rx_message_t *rx)
{
    Can_HwType mailbox;
    PduInfoType pdu;
    uint8 len = can_len_from_dlc((uint8) rx->dlc);
    uint8 data[64];
    uint8 i;

    if (len > 64U) {
        len = 64U;
    }
    for (i = 0U; i < len; i++) {
        data[i] = rx->data_8[i];
    }
    mailbox.ControllerId = ctrl;
    mailbox.Hoh = 0U; /* 由 HRH 表反查，见 can_hoh_of_fifo() */
    {
        uint8 h;
        for (h = 0U; h < s_can_config->HrhCount; h++) {
            if ((s_can_config->Hrhs[h].ControllerId == ctrl) &&
                (s_can_config->Hrhs[h].RxFifoIndex == fifo)) {
                mailbox.Hoh = (Can_HwHandleType) h;
                break;
            }
        }
    }
    if (rx->use_ext_id != 0U) {
        mailbox.CanId = ((rx->canfd_frame != 0U) ? CAN_ID_FD_EXT_FRAME : CAN_ID_EXT_FRAME) |
                        (rx->ext_id & CAN_ID_EXT_VALUE_MASK);
    } else {
        mailbox.CanId = ((rx->canfd_frame != 0U) ? CAN_ID_FD_STD_FRAME : CAN_ID_STD_FRAME) |
                        (rx->std_id & CAN_ID_STD_VALUE_MASK);
    }
    pdu.SduDataPtr = data;
    pdu.SduLength = (PduLengthType) len;
    CanIf_RxIndication(&mailbox, &pdu);
}

/*! 把某个 RXFIFO 里的报文全部取出并回调（保持 FIFO 不溢出） */
static void can_drain_rx_fifo(uint8 ctrl, uint8 fifo)
{
    mcan_rx_message_t rx;
    uint32 guard = 0U;

    while (mcan_read_rxfifo(can_base(ctrl), fifo, &rx) == status_success) {
        can_rx_indicate(ctrl, fifo, &rx);
        guard++;
        if (guard > 64U) { /* 单次 ISR 最多处理 64 帧，避免长期占中断 */
            break;
        }
    }
}

/*! 按 LEC 把 M_CAN 错误映射成 AUTOSAR Can_ErrorType；不认识的返回 0 */
static Can_ErrorType can_map_last_error_code(uint8 lec)
{
    switch (lec) {
    case 1U:
        return CAN_ERROR_CHECK_STUFFING_FAILED;
    case 2U:
        return CAN_ERROR_CHECK_FORM_FAILED;
    case 3U:
        return CAN_ERROR_CHECK_ACK_FAILED;
    case 4U:
        return CAN_ERROR_BIT_MONITORING1;
    case 5U:
        return CAN_ERROR_BIT_MONITORING0;
    case 6U:
        return CAN_ERROR_CHECK_CRC_FAILED;
    default:
        return (Can_ErrorType) 0U;
    }
}

/*! 处理一个控制器的错误类中断（bus-off / error passive / 其它错误） */
static void can_handle_error_events(uint8 ctrl)
{
    MCAN_Type *base = can_base(ctrl);
    uint8 lec = mcan_get_last_error_code(base);
    Can_ErrorType err = can_map_last_error_code(lec);

    if (mcan_is_in_busoff_state(base)) {
        s_busoff_reported[ctrl] = FALSE; /* 由 Can_MainFunction_BusOff 上报 */
        return;
    }
    if (mcan_is_in_err_passive_state(base)) {
        if (!s_errpassive_reported[ctrl]) {
            mcan_error_count_t ec;
            mcan_get_error_counter(base, &ec);
            s_errpassive_reported[ctrl] = TRUE;
            CanIf_ControllerErrorStatePassive(ctrl, (uint16) ec.receive_error_count,
                                              (uint16) ec.transmit_error_count);
        }
    } else {
        s_errpassive_reported[ctrl] = FALSE;
    }
    if ((err != (Can_ErrorType) 0U) && (lec != 7U)) {
        CanIf_ErrorNotification(ctrl, err);
    }
}

/*! 处理一个控制器的发送完成（TXBTO 位） */
static void can_handle_tx_complete(uint8 ctrl)
{
    MCAN_Type *base = can_base(ctrl);
    uint8 h;

    for (h = 0U; h < s_can_config->HthCount; h++) {
        const Can_HthConfigType *hth = &s_can_config->Hths[h];
        uint32 idx;
        if ((hth->ControllerId != ctrl) || (s_hth_busy[h] == FALSE)) {
            continue;
        }
        idx = (uint32) hth->TxBufferIndex;
        /* TXBRP=0 表示该专用发送缓冲的发送请求已完成；TXBTO=1 表示确实发到总线上
         * （M_CAN 的 TXBTO 只能由下一次 TXBAR 写 1 自动清除，不能直接写）。 */
        if ((mcan_is_transmit_request_pending(base, idx) == false) &&
            mcan_is_transmit_occurred(base, idx)) {
            s_hth_busy[h] = FALSE;
            CanIf_TxConfirmation(s_hth_pdu[h]);
        }
    }
}

/* ========================================================================
 * 中断分发（由 Can_PBcfg.c 的向量入口调用）
 * ======================================================================== */
void Can_Isr_Handler(uint8 ControllerId)
{
    MCAN_Type *base;
    uint32 flags;

    if ((s_can_config == NULL_PTR) || (ControllerId >= CAN_CONTROLLER_COUNT)) {
        return;
    }
    base = can_base(ControllerId);
    flags = mcan_get_interrupt_flags(base);

#if (CAN_RX_PROCESSING_INTERRUPT == 1U)
    if ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0U) {
        can_drain_rx_fifo(ControllerId, 0U);
    }
    if ((flags & MCAN_INT_RXFIFO1_NEW_MSG) != 0U) {
        can_drain_rx_fifo(ControllerId, 1U);
    }
    if ((flags & CAN_IRQ_LOST_MASK) != 0U) {
        /* SWS_Can_00395：报文被覆盖 -> runtime error CAN_E_DATALOST */
        (void) Det_ReportRuntimeError(CAN_MODULE_ID, ControllerId,
                                      CAN_API_ID_MAIN_FUNCTION_READ, CAN_E_DATALOST);
    }
#endif
    if ((flags & CAN_IRQ_ERR_MASK) != 0U) {
        can_handle_error_events(ControllerId);
    }
#if (CAN_TX_CONFIRM_IN_INTERRUPT == 1U)
    if ((flags & MCAN_INT_TX_COMPLETED) != 0U) {
        can_handle_tx_complete(ControllerId);
    }
#endif
    mcan_clear_interrupt_flags(base, flags);
}

/* ========================================================================
 * AUTOSAR API 实现
 * ======================================================================== */

void Can_Init(const Can_ConfigType *Config)
{
    uint8 c;
    Std_ReturnType st = E_OK;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (Config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_INIT, CAN_E_PARAM_POINTER);
        return;
    }
    if (s_can_config != NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_INIT, CAN_E_TRANSITION);
        return;
    }
#else
    if (Config == NULL_PTR) {
        return;
    }
#endif
    if ((Config->Controllers == NULL_PTR) || (Config->ControllerCount == 0U) ||
        (Config->ControllerCount > CAN_CONTROLLER_COUNT) || (Config->Hths == NULL_PTR) ||
        (Config->Hrhs == NULL_PTR)) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_INIT, CAN_E_PARAM_POINTER);
        return;
    }
    if ((Config->HthCount > CAN_MAX_HTH) || (Config->HrhCount > CAN_MAX_HRH) ||
        (Config->FilterCount > (CAN_MAX_FILTER_ELEMS * 2U * CAN_CONTROLLER_COUNT))) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_INIT, CAN_E_PARAM_HANDLE);
        return;
    }

    s_can_config = Config;
    for (c = 0U; c < CAN_CONTROLLER_COUNT; c++) {
        s_ctrl_state[c] = CAN_CS_UNINIT;
        s_ctrl_int_disable_cnt[c] = 0U;
    }
    for (c = 0U; c < CAN_MAX_HTH; c++) {
        s_hth_busy[c] = FALSE;
        s_hth_pdu[c] = 0U;
    }

    /* 逐个控制器初始化；SWS_Can_00408：任一控制器不是 UNINIT 则报 CAN_E_TRANSITION */
    for (c = 0U; c < Config->ControllerCount; c++) {
        if (can_init_controller(c, Config->Controllers[c].DefaultBaudrateConfigId) != E_OK) {
            st = E_NOT_OK;
            break;
        }
        s_ctrl_state[c] = CAN_CS_STOPPED;
    }
    if (st != E_OK) {
        for (c = 0U; c < CAN_CONTROLLER_COUNT; c++) {
            if (s_ctrl_state[c] != CAN_CS_UNINIT) {
                mcan_deinit(can_base(c));
                s_ctrl_state[c] = CAN_CS_UNINIT;
            }
        }
        s_can_config = NULL_PTR;
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_INIT, CAN_E_INIT_FAILED);
    }
}

void Can_DeInit(void)
{
    uint8 c;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_DEINIT, CAN_E_UNINIT);
        return;
    }
    /* SWS_Can_00281：所有控制器必须处于 STOPPED 才能 DeInit */
    for (c = 0U; c < s_can_config->ControllerCount; c++) {
        if (s_ctrl_state[c] != CAN_CS_STOPPED) {
            MCAL_DET_REPORT(CAN_MODULE_ID, c, CAN_API_ID_DEINIT, CAN_E_TRANSITION);
            return;
        }
    }
#else
    if (s_can_config == NULL_PTR) {
        return;
    }
#endif
    for (c = 0U; c < s_can_config->ControllerCount; c++) {
        MCAN_Type *base = can_base(c);
        mcan_disable_interrupts(base, 0xFFFFFFFFU);
        mcan_deinit(base);
        s_ctrl_state[c] = CAN_CS_UNINIT;
    }
    s_can_config = NULL_PTR;
}

void Can_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (versioninfo == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_GET_VERSION_INFO, CAN_E_PARAM_POINTER);
        return;
    }
#else
    if (versioninfo == NULL_PTR) {
        return;
    }
#endif
    versioninfo->vendorID = (uint16) CAN_VENDOR_ID;
    versioninfo->moduleID = (uint16) CAN_MODULE_ID;
    versioninfo->sw_major_version = (uint8) CAN_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8) CAN_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8) CAN_SW_PATCH_VERSION;
}

Std_ReturnType Can_SetBaudrate(uint8 Controller, uint16 BaudRateConfigID)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_SET_BAUDRATE, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_BAUDRATE, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
    if (can_find_baud_cfg(Controller, BaudRateConfigID) == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_BAUDRATE, CAN_E_PARAM_BAUDRATE);
        return E_NOT_OK;
    }
    /* SWS_Can_00256/00260：需要重新初始化时控制器必须处于 STOPPED */
    if (s_ctrl_state[Controller] != CAN_CS_STOPPED) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_BAUDRATE, CAN_E_TRANSITION);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount) ||
        (can_find_baud_cfg(Controller, BaudRateConfigID) == NULL_PTR) ||
        (s_ctrl_state[Controller] != CAN_CS_STOPPED)) {
        return E_NOT_OK;
    }
#endif
    if (can_init_controller(Controller, BaudRateConfigID) != E_OK) {
        return E_NOT_OK;
    }
    s_ctrl_state[Controller] = CAN_CS_STOPPED;
    return E_OK;
}

Std_ReturnType Can_SetControllerMode(uint8 Controller, Can_ControllerStateType Transition)
{
    MCAN_Type *base;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount)) {
        return E_NOT_OK;
    }
#endif
    base = can_base(Controller);

    switch (Transition) {
    case CAN_CS_STARTED:
        /* SWS_Can_00161：控制器处于 bus-off 时，请求 STARTED 即触发 bus-off 恢复序列。
         * 注意此时软件状态仍是 STARTED（bus-off 是硬件状态），不能按"非法迁移"处理。 */
        if ((s_ctrl_state[Controller] == CAN_CS_STARTED) && mcan_is_in_busoff_state(base)) {
            if (can_init_controller(Controller, s_ctrl_baud_id[Controller]) != E_OK) {
                return E_NOT_OK;
            }
            mcan_enter_normal_mode(base);
            if (can_wait_cccr_init(base, false) != E_OK) {
                return E_NOT_OK;
            }
            mcan_clear_interrupt_flags(base, 0xFFFFFFFFU);
            s_busoff_reported[Controller] = FALSE;
            s_errpassive_reported[Controller] = FALSE;
            CanIf_ControllerModeIndication(Controller, CAN_CS_STARTED);
            return E_OK;
        }
        /* SWS_Can_00409：只有 STOPPED/SLEEP 才允许进 STARTED */
        if ((s_ctrl_state[Controller] != CAN_CS_STOPPED) && (s_ctrl_state[Controller] != CAN_CS_SLEEP)) {
            MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_TRANSITION);
            return E_NOT_OK;
        }
        /* SWS_Can_00384：每次 STARTED 都用当前波特率配置重新初始化控制器 */
        if (can_init_controller(Controller, s_ctrl_baud_id[Controller]) != E_OK) {
            return E_NOT_OK;
        }
        if (mcan_is_in_busoff_state(base)) {
            /* 从 bus-off 恢复：SWS_Can_00161，STARTED 触发恢复序列 */
            (void) mcan_recover_from_busoff(base);
        }
        mcan_enter_normal_mode(base);
        /* M_CAN 的 CCCR 写是 AHB posted write，清 INIT 后必须轮询确认（SWS_Can_00262
         * "shall wait for limited time until the CAN controller is fully operational"） */
        if (can_wait_cccr_init(base, false) != E_OK) {
            return E_NOT_OK;
        }
        s_busoff_reported[Controller] = FALSE;
        s_errpassive_reported[Controller] = FALSE;
        mcan_clear_interrupt_flags(base, 0xFFFFFFFFU);
        s_ctrl_state[Controller] = CAN_CS_STARTED;
        CanIf_ControllerModeIndication(Controller, CAN_CS_STARTED);
        return E_OK;

    case CAN_CS_STOPPED:
        if ((s_ctrl_state[Controller] != CAN_CS_STARTED) && (s_ctrl_state[Controller] != CAN_CS_SLEEP)) {
            MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_TRANSITION);
            return E_NOT_OK;
        }
        /* SWS_Can_00263：停止参与总线 -> 进 INIT 模式（M_CAN 的"离开总线"状态） */
        mcan_enter_init_mode(base);
        if (can_wait_cccr_init(base, true) != E_OK) {
            return E_NOT_OK;
        }
        mcan_clear_interrupt_flags(base, 0xFFFFFFFFU);
        s_ctrl_state[Controller] = CAN_CS_STOPPED;
        CanIf_ControllerModeIndication(Controller, CAN_CS_STOPPED);
        return E_OK;

    case CAN_CS_SLEEP:
        /* SWS_Can_00283：只有 STOPPED 才能进 SLEEP；M_CAN 无 bus wakeup，本实现只做停机 */
        if (s_ctrl_state[Controller] != CAN_CS_STOPPED) {
            MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_TRANSITION);
            return E_NOT_OK;
        }
        mcan_enter_init_mode(base);
        if (can_wait_cccr_init(base, true) != E_OK) {
            return E_NOT_OK;
        }
        mcan_stop_clock(base);
        s_ctrl_state[Controller] = CAN_CS_SLEEP;
        CanIf_ControllerModeIndication(Controller, CAN_CS_SLEEP);
        return E_OK;

    case CAN_CS_UNINIT:
    default:
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_SET_CONTROLLER_MODE, CAN_E_TRANSITION);
        return E_NOT_OK;
    }
}

void Can_DisableControllerInterrupts(uint8 Controller)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_DISABLE_CONTROLLER_INTERRUPTS, CAN_E_UNINIT);
        return;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_DISABLE_CONTROLLER_INTERRUPTS, CAN_E_PARAM_CONTROLLER);
        return;
    }
    if ((s_ctrl_state[Controller] == CAN_CS_UNINIT) || (s_ctrl_state[Controller] == CAN_CS_SLEEP)) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_DISABLE_CONTROLLER_INTERRUPTS, CAN_E_TRANSITION);
        return;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount)) {
        return;
    }
#endif
    if (s_ctrl_int_disable_cnt[Controller] == 0U) {
        mcan_disable_interrupts(can_base(Controller), s_ctrl_int_mask[Controller]);
    }
    if (s_ctrl_int_disable_cnt[Controller] < CAN_INT_DISABLE_COUNT_MAX) {
        s_ctrl_int_disable_cnt[Controller]++;
    }
}

void Can_EnableControllerInterrupts(uint8 Controller)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_ENABLE_CONTROLLER_INTERRUPTS, CAN_E_UNINIT);
        return;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_ENABLE_CONTROLLER_INTERRUPTS, CAN_E_PARAM_CONTROLLER);
        return;
    }
    if (s_ctrl_state[Controller] == CAN_CS_UNINIT) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_ENABLE_CONTROLLER_INTERRUPTS, CAN_E_TRANSITION);
        return;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount)) {
        return;
    }
#endif
    if (s_ctrl_int_disable_cnt[Controller] > 0U) {
        s_ctrl_int_disable_cnt[Controller]--;
        if (s_ctrl_int_disable_cnt[Controller] == 0U) {
            mcan_enable_interrupts(can_base(Controller), s_ctrl_int_mask[Controller]);
        }
    }
}

Std_ReturnType Can_CheckWakeup(uint8 Controller)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_CHECK_WAKEUP, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_CHECK_WAKEUP, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount)) {
        return E_NOT_OK;
    }
#endif
    /* 已知限制：M_CAN 控制器本身没有 CAN 总线唤醒检测单元（见第 47 章"待确认项"）。
     * TCAN1044AV 也无 INH/RXD 唤醒输出，板上未接唤醒线，因此恒返回"未唤醒"。 */
    return E_NOT_OK;
}

Std_ReturnType Can_GetControllerErrorState(uint8 Controller, Can_ErrorStateType *ErrorStatePtr)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_GET_CONTROLLER_ERROR_STATE, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_ERROR_STATE, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
    if (ErrorStatePtr == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_ERROR_STATE, CAN_E_PARAM_POINTER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount) ||
        (ErrorStatePtr == NULL_PTR)) {
        return E_NOT_OK;
    }
#endif
    {
        MCAN_Type *base = can_base(Controller);
        mcan_error_state_t st = mcan_error_state_active;
        (void) mcan_get_error_state(base, &st);
        switch (st) {
        case mcan_error_state_bus_off:
            *ErrorStatePtr = CAN_ERRORSTATE_BUSOFF;
            break;
        case mcan_error_state_passive:
            *ErrorStatePtr = CAN_ERRORSTATE_PASSIVE;
            break;
        case mcan_error_state_warning:
            /* AUTOSAR 只有三态，Error Warning 仍属 ACTIVE（真正 passive 由 EP 位判定） */
            *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
            break;
        case mcan_error_state_active:
        default:
            *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
            break;
        }
    }
    return E_OK;
}

Std_ReturnType Can_GetControllerMode(uint8 Controller, Can_ControllerStateType *ControllerModePtr)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_GET_CONTROLLER_MODE, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_MODE, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
    if (ControllerModePtr == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_MODE, CAN_E_PARAM_POINTER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount) ||
        (ControllerModePtr == NULL_PTR)) {
        return E_NOT_OK;
    }
#endif
    *ControllerModePtr = s_ctrl_state[Controller];
    return E_OK;
}

Std_ReturnType Can_GetControllerRxErrorCounter(uint8 Controller, uint8 *RxErrorCounterPtr)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_GET_CONTROLLER_RX_ERROR_COUNTER, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_RX_ERROR_COUNTER, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
    if (RxErrorCounterPtr == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_RX_ERROR_COUNTER, CAN_E_PARAM_POINTER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount) ||
        (RxErrorCounterPtr == NULL_PTR)) {
        return E_NOT_OK;
    }
#endif
    {
        mcan_error_count_t ec;
        mcan_get_error_counter(can_base(Controller), &ec);
        *RxErrorCounterPtr = ec.receive_error_count;
    }
    return E_OK;
}

Std_ReturnType Can_GetControllerTxErrorCounter(uint8 Controller, uint8 *TxErrorCounterPtr)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_GET_CONTROLLER_TX_ERROR_COUNTER, CAN_E_UNINIT);
        return E_NOT_OK;
    }
    if (Controller >= s_can_config->ControllerCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_TX_ERROR_COUNTER, CAN_E_PARAM_CONTROLLER);
        return E_NOT_OK;
    }
    if (TxErrorCounterPtr == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, Controller, CAN_API_ID_GET_CONTROLLER_TX_ERROR_COUNTER, CAN_E_PARAM_POINTER);
        return E_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Controller >= s_can_config->ControllerCount) ||
        (TxErrorCounterPtr == NULL_PTR)) {
        return E_NOT_OK;
    }
#endif
    {
        mcan_error_count_t ec;
        mcan_get_error_counter(can_base(Controller), &ec);
        *TxErrorCounterPtr = ec.transmit_error_count;
    }
    return E_OK;
}

Std_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType *PduInfo)
{
    mcan_tx_frame_t frame;
    const Can_HthConfigType *hth;
    const Can_BaudrateConfigItemType *bc;
    Can_IdType id;
    boolean is_ext;
    boolean is_fd;
    uint8 len;
    uint8 dlc;
    uint8 i;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_WRITE, CAN_E_UNINIT);
        return CAN_NOT_OK;
    }
    if (Hth >= s_can_config->HthCount) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_WRITE, CAN_E_PARAM_HANDLE);
        return CAN_NOT_OK;
    }
    if (PduInfo == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_WRITE, CAN_E_PARAM_POINTER);
        return CAN_NOT_OK;
    }
    if (PduInfo->sdu == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_WRITE, CAN_E_PARAM_POINTER);
        return CAN_NOT_OK;
    }
    if ((PduInfo->length > 64U) ||
        ((CAN_ID_IS_FD(PduInfo->id) == 0U) && (PduInfo->length > 8U))) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_WRITE, CAN_E_PARAM_DATA_LENGTH);
        return CAN_NOT_OK;
    }
#else
    if ((s_can_config == NULL_PTR) || (Hth >= s_can_config->HthCount) || (PduInfo == NULL_PTR) ||
        (PduInfo->sdu == NULL_PTR) || (PduInfo->length > 64U)) {
        return CAN_NOT_OK;
    }
#endif

    hth = &s_can_config->Hths[Hth];
    bc = can_find_baud_cfg(hth->ControllerId, s_ctrl_baud_id[hth->ControllerId]);
    if ((bc == NULL_PTR) || (s_ctrl_state[hth->ControllerId] != CAN_CS_STARTED)) {
        return CAN_NOT_OK;
    }
    if (s_hth_busy[Hth] != FALSE) {
        return CAN_BUSY; /* SWS_Can_00213：发送对象忙，直接返回 CAN_BUSY */
    }

    /* 组帧：Can_IdType 高 2 位决定标准/扩展、CAN 2.0/FD */
    id = PduInfo->id;
    is_ext = (CAN_ID_IS_EXTENDED(id) != 0U) ? TRUE : FALSE;
    is_fd = (CAN_ID_IS_FD(id) != 0U) ? TRUE : FALSE;

    __builtin_memset(&frame, 0, sizeof(frame));
    len = PduInfo->length;
    dlc = can_dlc_from_len(len);
    if (is_fd != TRUE) {
        /* 控制器不是 FD 的话，AUTOSAR 要求退化为经典帧发送（<=8 字节） */
        if (bc->Timing.FdEnabled != TRUE) {
            is_fd = FALSE;
        }
        if (len > 8U) {
            len = 8U;
            dlc = 8U;
        }
        frame.canfd_frame = 0U;
        frame.bitrate_switch = 0U;
    } else {
        if (bc->Timing.FdEnabled != TRUE) {
            /* 请求 FD 帧但当前配置不是 FD：按经典帧发（SWS §7.13） */
            if (len > 8U) {
                return CAN_NOT_OK;
            }
            frame.canfd_frame = 0U;
            frame.bitrate_switch = 0U;
        } else {
            frame.canfd_frame = 1U;
            frame.bitrate_switch = (bc->Timing.BrsEnabled != FALSE) ? 1U : 0U;
        }
    }
    if (is_ext != FALSE) {
        frame.use_ext_id = 1U;
        frame.ext_id = (uint32) (id & CAN_ID_EXT_VALUE_MASK);
    } else {
        frame.use_ext_id = 0U;
        frame.std_id = (uint32) (id & CAN_ID_STD_VALUE_MASK);
    }
    frame.dlc = dlc;
    frame.rtr = 0U;
    frame.error_state_indicator = 0U;
    frame.event_fifo_control = 0U;
    frame.message_marker_l = (uint32) (PduInfo->swPduHandle & 0xFFU);
    for (i = 0U; i < len; i++) {
        frame.data_8[i] = PduInfo->sdu[i];
    }

    if (mcan_transmit_via_txbuf_nonblocking(can_base(hth->ControllerId),
                                            (uint32) hth->TxBufferIndex, &frame) != status_success) {
        return CAN_BUSY;
    }
    s_hth_busy[Hth] = TRUE;
    s_hth_pdu[Hth] = PduInfo->swPduHandle;
    return CAN_OK;
}

void Can_MainFunction_Write(void)
{
    uint8 c;
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_MAIN_FUNCTION_WRITE, CAN_E_UNINIT);
        return;
    }
#endif
    if (s_can_config == NULL_PTR) {
        return;
    }
#if (CAN_TX_CONFIRM_IN_INTERRUPT == 0U)
    /* 轮询模式：主函数里查 TXBTO 完成发送确认 */
    for (c = 0U; c < s_can_config->ControllerCount; c++) {
        if (s_ctrl_state[c] == CAN_CS_STARTED) {
            can_handle_tx_complete(c);
        }
    }
#else
    for (c = 0U; c < CAN_CONTROLLER_COUNT; c++) {
        (void) c;
    }
#endif
}

void Can_MainFunction_Read(void)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_MAIN_FUNCTION_READ, CAN_E_UNINIT);
        return;
    }
#endif
    if (s_can_config == NULL_PTR) {
        return;
    }
#if (CAN_RX_PROCESSING_INTERRUPT == 0U)
    {
        uint8 c;
        for (c = 0U; c < s_can_config->ControllerCount; c++) {
            if (s_ctrl_state[c] != CAN_CS_STARTED) {
                continue;
            }
            if (s_can_config->Controllers[c].RxFifo0Count != 0U) {
                can_drain_rx_fifo(c, 0U);
            }
            if (s_can_config->Controllers[c].RxFifo1Count != 0U) {
                can_drain_rx_fifo(c, 1U);
            }
            if ((mcan_get_interrupt_flags(can_base(c)) & CAN_IRQ_LOST_MASK) != 0U) {
                mcan_clear_interrupt_flags(can_base(c), CAN_IRQ_LOST_MASK);
                (void) Det_ReportRuntimeError(CAN_MODULE_ID, c,
                                              CAN_API_ID_MAIN_FUNCTION_READ, CAN_E_DATALOST);
            }
        }
    }
#endif
}

void Can_MainFunction_BusOff(void)
{
    uint8 c;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_MAIN_FUNCTION_BUSOFF, CAN_E_UNINIT);
        return;
    }
#endif
    if (s_can_config == NULL_PTR) {
        return;
    }
    for (c = 0U; c < s_can_config->ControllerCount; c++) {
        if (mcan_is_in_busoff_state(can_base(c))) {
            if (s_busoff_reported[c] == FALSE) {
                s_busoff_reported[c] = TRUE;
                CanIf_ControllerBusOff(c);
            }
        } else {
            s_busoff_reported[c] = FALSE;
        }
    }
}

void Can_MainFunction_Wakeup(void)
{
    uint8 c;

#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_MAIN_FUNCTION_WAKEUP, CAN_E_UNINIT);
        return;
    }
#endif
    if (s_can_config == NULL_PTR) {
        return;
    }
    /* 已知限制：无总线唤醒源（同 Can_CheckWakeup）；这里只处理 SLEEP -> 外部请求唤醒的通知 */
    for (c = 0U; c < s_can_config->ControllerCount; c++) {
        (void) c;
    }
}

void Can_MainFunction_Mode(void)
{
#if (CAN_DEV_ERROR_DETECT == STD_ON)
    if (s_can_config == NULL_PTR) {
        MCAL_DET_REPORT(CAN_MODULE_ID, 0U, CAN_API_ID_MAIN_FUNCTION_MODE, CAN_E_UNINIT);
        return;
    }
#endif
    /* 本实现的状态迁移是同步完成的（Can_SetControllerMode 里已调
     * CanIf_ControllerModeIndication），因此这里没有待处理项；
     * 保留该 API 以符合 SWS 的 API 完整性要求（第 47 章 API 完成度表）。 */
}

/* ========================================================================
 * CanIf 回调的 weak 桩（例程可覆盖）
 * ======================================================================== */
MCAL_WEAK void CanIf_TxConfirmation(PduIdType CanTxPduId)
{
    MCAL_UNUSED(CanTxPduId);
}

MCAL_WEAK void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr)
{
    MCAL_UNUSED(Mailbox);
    MCAL_UNUSED(PduInfoPtr);
}

MCAL_WEAK void CanIf_ControllerBusOff(uint8 ControllerId)
{
    MCAL_UNUSED(ControllerId);
}

MCAL_WEAK void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode)
{
    MCAL_UNUSED(ControllerId);
    MCAL_UNUSED(ControllerMode);
}

MCAL_WEAK void CanIf_ControllerWakeup(uint8 ControllerId)
{
    MCAL_UNUSED(ControllerId);
}

MCAL_WEAK void CanIf_ControllerErrorStatePassive(uint8 ControllerId, uint16 RxErrorCounter, uint16 TxErrorCounter)
{
    MCAL_UNUSED(ControllerId);
    MCAL_UNUSED(RxErrorCounter);
    MCAL_UNUSED(TxErrorCounter);
}

MCAL_WEAK void CanIf_ErrorNotification(uint8 ControllerId, Can_ErrorType CanError)
{
    MCAL_UNUSED(ControllerId);
    MCAL_UNUSED(CanError);
}

#define CAN_STOP_SEC_CODE
#include "MemMap.h"
