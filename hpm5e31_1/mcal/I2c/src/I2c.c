/*
 * I2c.c —— AUTOSAR CP I2C Driver（模块 ID 113）实现，HPM5E31 I2C_Type
 *
 * 参考：AUTOSAR_CP_SWS_I2CDriver R25-11 §8.3（I2C_Init/DeInit/SetupEB/AsyncTransmit/SyncTransmit/
 *       GetSequenceResult/GetChannelStatus/MainFunction）、§7.3（DevError/RuntimeError）；
 *       HPM5E31 RM "I2C" 章：SETUP（速度/主从）、ADDR（本机地址）、DATA（FIFO 入口）、
 *       STATUS（ADDRESS_HIT/TRANSACTION_COMPLETE/FIFO 状态）、INTEN、CMD（读写命令）。
 *
 * 实现要点：
 *   - 控制器（主机）：阻塞 write / read / write+read；NACK、总线忙分别按 Runtime Error
 *     I2C_E_NACK_RECEIVED / I2C_E_ARBITRATION_FAILURE 上报 Det。
 *   - 目标（从机）：I2C3 地址命中中断里按方向武装 DMA（收/发），事务完成中断置标志；
 *     目标 Job 在序列里"预备"后立即推进（由控制器发时钟）。
 *   - 异步模式在主函数上下文执行（SWS 允许的 polling 语义），完成时回调 SeqEndNotification。
 *   - 目标（从机）通道初始化后会把 SETUP.T_SUDAT 抬到"标准模式 250 ns"下限：SDK 在
 *     fast/fast_plus 下算出 0（100 ns），实测 400 kHz 目标发送方向会被控制器读成 0x00。
 */
#include "I2c.h"
#include "Det.h"

#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_i2c_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"

#if (I2C_DEV_ERROR_DETECT == STD_ON)
#define I2C_DET(api, err)  MCAL_DET_REPORT(I2C_MODULE_ID, 0u, (api), (err))
#else
#define I2C_DET(api, err)  ((void)0)
#endif

static const I2C_ConfigType *s_cfg = NULL;
static boolean s_init = FALSE;
static I2C_SequenceResultType s_seqRes[I2C_SEQUENCE_COUNT];
static I2C_ChannelStatusType  s_chStatus[I2C_CHANNEL_COUNT];
static uint32 s_runtimeErrCnt;

/* Job 的 EB 缓冲（SWS：I2C 只有 EB） */
static const uint8 *s_tx[I2C_JOB_COUNT];
static uint8       *s_rx[I2C_JOB_COUNT];
static uint16       s_len[I2C_JOB_COUNT];
static I2C_AddressType s_node[I2C_JOB_COUNT];

/* 目标模式状态 */
static volatile uint8  s_tgtTxDma;
static volatile uint8  s_tgtFirstPreloaded;
static volatile uint8  s_tgtAddrHit;
static volatile uint8  s_tgtDone;
static volatile uint8  s_tgtErr;
static uint16          s_tgtLen;
static const uint8    *s_tgtTxBuf;
static uint8          *s_tgtRxBuf;

/* 当前异步序列 */
static volatile uint8 s_asyncSeq = 0xFFu;

static I2C_Type *i2c_base(I2C_ChannelType ch)
{
    return (I2C_Type *)s_cfg->Channels[ch].Base;
}

static void i2c_status_poll_guard(uint32 n)
{
    volatile uint32 i;
    for (i = 0u; i < n; i++) { }
}

/* ---------- 控制器（主机）传输 ---------- */
static Std_ReturnType i2c_master_job(I2C_JobType job)
{
    I2C_ChannelType ch = s_cfg->Jobs[job].Channel;
    I2C_Type *p = i2c_base(ch);
    I2C_AddressType addr = (s_node[job] != 0u) ? s_node[job] : s_cfg->Jobs[job].NodeAddress;
    hpm_stat_t st = status_success;

    if ((s_tx[job] != NULL) && (s_len[job] != 0u)) {
        st = i2c_master_write(p, addr, (uint8_t *)s_tx[job], s_len[job]);
    }
    if ((st == status_success) && (s_rx[job] != NULL) && (s_len[job] != 0u)) {
        st = i2c_master_read(p, addr, s_rx[job], s_len[job]);
    }
    if (st == status_success) {
        return E_OK;
    }
    /* 运行时错误分类（SWS §7.3.2） */
    s_runtimeErrCnt++;
    if (st == status_i2c_no_ack) {
        (void)Det_ReportRuntimeError((uint16)I2C_MODULE_ID, ch, I2C_API_ID_SYNC_TRANSMIT, I2C_E_NACK_RECEIVED);
    } else if (st == status_i2c_bus_busy) {
        (void)Det_ReportRuntimeError((uint16)I2C_MODULE_ID, ch, I2C_API_ID_SYNC_TRANSMIT, I2C_E_ARBITRATION_FAILURE);
    } else {
        (void)Det_ReportRuntimeError((uint16)I2C_MODULE_ID, ch, I2C_API_ID_SYNC_TRANSMIT, I2C_E_ARBITRATION_FAILURE);
    }
    return E_NOT_OK;
}

/* ---------- 目标（从机）预备：登记本次缓冲，清标志，等地址命中 ---------- */
static void i2c_target_prepare(I2C_JobType job)
{
    I2C_ChannelType ch;
    I2C_Type *p;

    s_tgtTxBuf = s_tx[job];
    s_tgtRxBuf = s_rx[job];
    s_tgtLen   = s_len[job];
    s_tgtAddrHit = 0u;
    s_tgtDone = 0u;
    s_tgtErr  = 0u;
    s_tgtTxDma = 0u;
    s_tgtFirstPreloaded = 0u;

    (void)ch;
    (void)p;
    /* 说明：曾尝试在地址命中前预装首字节以消除 400 kHz 下的首字节窗口，但 I2C 从机事务计数
     * 与 DMA 长度必须一致，预装后计数不匹配反而导致末字节错误与 9 ms 级延迟，故回退。
     * 400 kHz 首字节问题列入待确认项（章节 §46.11）。 */
}

static boolean i2c_target_wait_done(void)
{
    uint32 guard = I2C_TIMEOUT_POLLS;
    while ((s_tgtDone == 0u) && (guard-- != 0u)) { }
    return (s_tgtDone != 0u) && (s_tgtErr == 0u);
}

/* ---------- 序列执行（同步路径与主函数路径共用） ---------- */
static I2C_SequenceResultType i2c_run_sequence(I2C_SequenceType seq)
{
    const I2C_SequenceConfigType *sc = &s_cfg->Sequences[seq];
    I2C_SequenceResultType result = I2C_SEQ_OK;
    uint8 i;
    uint8 targetUsed = 0u;

    for (i = 0u; i < sc->JobCount; i++) {
        I2C_JobType job = sc->JobList[i];
        I2C_ChannelType ch = s_cfg->Jobs[job].Channel;

        s_chStatus[ch] = I2C_BUSY;
        if (s_cfg->Channels[ch].IsTarget) {
            i2c_target_prepare(job);
            targetUsed = 1u;
        } else {
            if (i2c_master_job(job) != E_OK) {
                result = I2C_SEQ_NACK;
            }
        }
        s_chStatus[ch] = I2C_IDLE;
    }
    if ((result == I2C_SEQ_OK) && (targetUsed != 0u)) {
        if (!i2c_target_wait_done()) {
            result = I2C_SEQ_NACK;
        }
    }
    return result;
}

static void i2c_notify_seq(I2C_SequenceType seq)
{
    if (s_cfg->Sequences[seq].SeqEndNotification != NULL) {
        s_cfg->Sequences[seq].SeqEndNotification();
    }
}

/* ---------- 目标（从机）侧数据建立时间修正 ----------
 * 现象：目标通道按 400 kHz（i2c_mode_fast）配置时，控制器读回（目标发送方向）的数据
 *       大量为 0x00（logs/20261102-153604-i2c_master_slave.log：rdloop 50 次全 FAIL、
 *       错误集中在低位字节）；把目标侧 SETUP.T_SUDAT 从 0 抬到 12 后 100 次全 PASS，
 *       再改回 0 又 100 次全 FAIL（A/B 对照见 logs/20261102-153857）。
 * 原因：SDK i2c_configure_timing() 对 fast/fast_plus 取 I2C 规范下限 t_sudat=0，
 *       80 MHz 功能时钟下数据建立时间只有约 100 ns（2*Tclk+(2+T_SP+0)*Tclk），
 *       恰好等于快速模式的最小值，目标发送方向没有余量；标准模式算出来是 12（250 ns）。
 * 处理：目标通道不产生 SCL，抬高 T_SUDAT 只改变从机侧采样/建立窗口，不改变总线速率
 *       （实测 400k/1M 传输周期不变）。这里按"标准模式 250 ns 下限"统一修正。
 *       主控通道不受影响：单独把控制器侧 T_SUDAT 抬到 12 仍 100% 失败。
 */
static void i2c_target_timing_fixup(I2C_Type *p, uint32 clk_hz)
{
    uint32 tpclk;      /* 100 ps 为单位的功能时钟周期 */
    uint32 t_sp;
    uint32 need;
    uint32 t_sudat;

    if (clk_hz == 0u) { return; }
    tpclk = 10000000000u / clk_hz;
    if (tpclk == 0u) { return; }
    t_sp = 500u / tpclk;
    if (t_sp > 7u) { t_sp = 7u; }
    /* SetupTime = 2*Tclk + (2 + T_SP + T_SUDAT)*Tclk >= 250 ns（标准模式下限） */
    need = (2500u > (2u * tpclk)) ? ((2500u - 2u * tpclk) / tpclk) : 0u;
    t_sudat = (need > (2u + t_sp)) ? (need - 2u - t_sp) : 0u;
    if (t_sudat > 31u) { t_sudat = 31u; }
    if (I2C_SETUP_T_SUDAT_GET(p->SETUP) < t_sudat) {
        p->SETUP = (p->SETUP & ~I2C_SETUP_T_SUDAT_MASK) | I2C_SETUP_T_SUDAT_SET(t_sudat);
    }
}

/* ---------- SWS 8.3.1 I2C_Init ---------- */
void I2C_Init(const I2C_ConfigType *ConfigPtr)
{
    I2C_ChannelType ch;

    if (s_init) {
        I2C_DET(I2C_API_ID_INIT, I2C_E_WRONG_CONDITION);
        return;
    }
    if (ConfigPtr == NULL) {
        I2C_DET(I2C_API_ID_INIT, I2C_E_PARAM_POINTER);
        return;
    }
    s_cfg = ConfigPtr;

    for (ch = 0u; ch < I2C_CHANNEL_COUNT; ch++) {
        const I2C_ChannelConfigType *c = &s_cfg->Channels[ch];
        I2C_Type *p = (I2C_Type *)c->Base;
        uint32 clk = 0u;
        i2c_config_t cfg;

        if (c->PinInit != NULL) { c->PinInit(); }
        if (c->ClockEnable != NULL) { clk = c->ClockEnable(); }
        if (clk == 0u) { clk = MCAL_BOARD_XTAL_HZ; }

        cfg.is_10bit_addressing = false;
        cfg.i2c_mode = (c->BaudrateKHz <= 100u) ? i2c_mode_normal
                     : ((c->BaudrateKHz <= 400u) ? i2c_mode_fast : i2c_mode_fast_plus);
        if (c->IsTarget) {
            (void)i2c_init_slave(p, clk, &cfg, c->TargetAddress);
            i2c_target_timing_fixup(p, clk);
            i2c_enable_irq(p, (uint32_t)I2C_EVENT_ADDRESS_HIT | (uint32_t)I2C_EVENT_TRANSACTION_COMPLETE);
            if (c->IrqNum != 0u) {
                intc_m_enable_irq_with_priority((uint32_t)c->IrqNum, 2u);
            }
        } else {
            (void)i2c_init_master(p, clk, &cfg);
        }
        s_chStatus[ch] = I2C_IDLE;
    }
    for (ch = 0u; ch < I2C_SEQUENCE_COUNT; ch++) {
        s_seqRes[ch] = I2C_SEQ_OK;
    }
    for (ch = 0u; ch < I2C_JOB_COUNT; ch++) {
        s_tx[ch] = NULL;
        s_rx[ch] = NULL;
        s_len[ch] = 0u;
        s_node[ch] = 0u;
    }
    s_runtimeErrCnt = 0u;
    s_asyncSeq = 0xFFu;
    s_init = TRUE;
}

/* ---------- SWS 8.3.2 I2C_DeInit ---------- */
void I2C_DeInit(void)
{
    I2C_ChannelType ch;

    if (!s_init) {
        I2C_DET(I2C_API_ID_DEINIT, I2C_E_UNINIT);
        return;
    }
    for (ch = 0u; ch < I2C_CHANNEL_COUNT; ch++) {
        const I2C_ChannelConfigType *c = &s_cfg->Channels[ch];
        I2C_Type *p = (I2C_Type *)c->Base;
        i2c_disable_irq(p, 0xFFFFFFFFu);
        i2c_reset(p);
        s_chStatus[ch] = I2C_IDLE;
    }
    s_init = FALSE;
    s_cfg = NULL;
}

/* ---------- SWS 8.3.3 I2C_SetupEB ---------- */
Std_ReturnType I2C_SetupEB(I2C_JobType JobId, I2C_AddressType NodeAddress,
                           const I2C_DataPtrType TxDataBufferPtr, I2C_DataPtrType RxDataBufferPtr,
                           I2C_NumberOfDataType Length)
{
    if (!s_init) {
        I2C_DET(I2C_API_ID_SETUP_EB, I2C_E_UNINIT);
        return E_NOT_OK;
    }
    if (JobId >= I2C_JOB_COUNT) {
        I2C_DET(I2C_API_ID_SETUP_EB, I2C_E_PARAM_JOB);
        return E_NOT_OK;
    }
    if ((TxDataBufferPtr == NULL) && (RxDataBufferPtr == NULL)) {
        I2C_DET(I2C_API_ID_SETUP_EB, I2C_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if ((Length == 0u) || (Length > I2C_MAX_DATA_LENGTH)) {
        I2C_DET(I2C_API_ID_SETUP_EB, I2C_E_PARAM_JOB);
        return E_NOT_OK;
    }
    s_tx[JobId]   = TxDataBufferPtr;
    s_rx[JobId]   = RxDataBufferPtr;
    s_len[JobId]  = Length;
    s_node[JobId] = NodeAddress;
    return E_OK;
}

/* ---------- SWS 8.3.4 I2C_AsyncTransmit ---------- */
Std_ReturnType I2C_AsyncTransmit(I2C_SequenceType SequenceId)
{
    if (!s_init) {
        I2C_DET(I2C_API_ID_ASYNC_TRANSMIT, I2C_E_UNINIT);
        return E_NOT_OK;
    }
    if (SequenceId >= I2C_SEQUENCE_COUNT) {
        I2C_DET(I2C_API_ID_ASYNC_TRANSMIT, I2C_E_PARAM_SEQUENCE);
        return E_NOT_OK;
    }
    if (s_asyncSeq != 0xFFu) {
        I2C_DET(I2C_API_ID_ASYNC_TRANSMIT, I2C_E_WRONG_CONDITION);
        return E_NOT_OK;
    }
    s_asyncSeq = (uint8)SequenceId;
    s_seqRes[SequenceId] = I2C_SEQ_PENDING;
    return E_OK;
}

/* ---------- SWS 8.3.5 I2C_SyncTransmit ---------- */
Std_ReturnType I2C_SyncTransmit(I2C_SequenceType SequenceId)
{
    I2C_SequenceResultType r;

    if (!s_init) {
        I2C_DET(I2C_API_ID_SYNC_TRANSMIT, I2C_E_UNINIT);
        return E_NOT_OK;
    }
    if (SequenceId >= I2C_SEQUENCE_COUNT) {
        I2C_DET(I2C_API_ID_SYNC_TRANSMIT, I2C_E_PARAM_SEQUENCE);
        return E_NOT_OK;
    }
    if (s_asyncSeq != 0xFFu) {
        I2C_DET(I2C_API_ID_SYNC_TRANSMIT, I2C_E_WRONG_CONDITION);
        return E_NOT_OK;
    }
    r = i2c_run_sequence(SequenceId);
    s_seqRes[SequenceId] = r;
    i2c_notify_seq(SequenceId);
    return (r == I2C_SEQ_OK) ? E_OK : E_NOT_OK;
}

/* ---------- SWS 8.3.7 I2C_GetSequenceResult ---------- */
I2C_SequenceResultType I2C_GetSequenceResult(I2C_SequenceType SequenceId)
{
    if (!s_init) {
        I2C_DET(I2C_API_ID_GET_SEQUENCE_RESULT, I2C_E_UNINIT);
        return I2C_SEQ_FAILED;
    }
    if (SequenceId >= I2C_SEQUENCE_COUNT) {
        I2C_DET(I2C_API_ID_GET_SEQUENCE_RESULT, I2C_E_PARAM_SEQUENCE);
        return I2C_SEQ_FAILED;
    }
    return s_seqRes[SequenceId];
}

/* ---------- SWS 8.3.8 I2C_GetChannelStatus ---------- */
I2C_ChannelStatusType I2C_GetChannelStatus(I2C_ChannelType Channel)
{
    if (!s_init) {
        I2C_DET(I2C_API_ID_GET_CHANNEL_STATUS, I2C_E_UNINIT);
        return I2C_IDLE;
    }
    if (Channel >= I2C_CHANNEL_COUNT) {
        I2C_DET(I2C_API_ID_GET_CHANNEL_STATUS, I2C_E_PARAM_UNIT);
        return I2C_IDLE;
    }
    return s_chStatus[Channel];
}

/* ---------- SWS 8.3.6 I2C_GetVersionInfo ---------- */
void I2C_GetVersionInfo(Std_VersionInfoType *VersionInfo)
{
    if (VersionInfo == NULL) {
        I2C_DET(I2C_API_ID_GET_VERSION_INFO, I2C_E_PARAM_POINTER);
        return;
    }
    VersionInfo->vendorID         = (uint16)I2C_VENDOR_ID;
    VersionInfo->moduleID         = (uint16)I2C_MODULE_ID;
    VersionInfo->sw_major_version = (uint8)I2C_SW_MAJOR_VERSION;
    VersionInfo->sw_minor_version = (uint8)I2C_SW_MINOR_VERSION;
    VersionInfo->sw_patch_version = (uint8)I2C_SW_PATCH_VERSION;
}

/* ---------- SWS 8.5.1 I2C_MainFunction ---------- */
void I2C_MainFunction(void)
{
    I2C_SequenceType seq = (I2C_SequenceType)s_asyncSeq;

    if (!s_init) {
        return;
    }
    if (seq == (I2C_SequenceType)0xFFu) {
        return;
    }
    s_seqRes[seq] = i2c_run_sequence(seq);
    s_asyncSeq = 0xFFu;
    i2c_notify_seq(seq);
}

uint32 I2C_GetRuntimeErrorCount(void)
{
    return s_runtimeErrCnt;
}

/* ---------- 目标模式中断：地址命中按方向武装 DMA，事务完成置标志 ---------- */
void I2C_Isr_Channel(I2C_ChannelType Channel)
{
    const I2C_ChannelConfigType *c;
    I2C_Type *p;
    DMAV2_Type *dma;
    DMAMUX_Type *mux;
    dma_handshake_config_t dc;
    uint32_t st;
    uint32_t bytes;

    if ((!s_init) || (Channel >= I2C_CHANNEL_COUNT)) {
        return;
    }
    c = &s_cfg->Channels[Channel];
    if (!c->IsTarget) {
        return;
    }
    p = (I2C_Type *)c->Base;
    dma = (DMAV2_Type *)c->DmaBase;
    mux = (DMAMUX_Type *)c->DmamuxBase;
    st = i2c_get_status(p);
    bytes = (uint32_t)s_tgtLen;

    if ((st & (uint32_t)I2C_EVENT_ADDRESS_HIT) != 0u) {
        s_tgtAddrHit++;
        if (I2C_DIR_SLAVE_READ == i2c_get_direction(p)) {
            /* 控制器写 -> 目标收 */
            i2c_dma_disable(p);
            dma_default_handshake_config(dma, &dc);
            dc.ch_index = c->RxDmaChannel;
            dc.src = (uint32_t)&p->DATA;
            dc.src_fixed = true;
            dc.dst = (uint32_t)s_tgtRxBuf;
            dc.dst_fixed = false;
            dc.data_width = DMA_TRANSFER_WIDTH_BYTE;
            dc.size_in_byte = bytes;
            dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, c->RxDmaChannel),
                          c->RxDmaSrc, true);
            if (dma_setup_handshake(dma, &dc, true) != status_success) {
                s_tgtErr = 1u;
            }
            (void)i2c_slave_dma_transfer(p, (uint32_t)s_tgtLen);
        } else {
            /* 控制器读 -> 目标发 */
            i2c_dma_disable(p);
            dma_default_handshake_config(dma, &dc);
            dc.ch_index = c->RxDmaChannel;
            dc.src = (uint32_t)s_tgtTxBuf;
            dc.src_fixed = false;
            dc.dst = (uint32_t)&p->DATA;
            dc.dst_fixed = true;
            dc.data_width = DMA_TRANSFER_WIDTH_BYTE;
            dc.size_in_byte = bytes;
            dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, c->RxDmaChannel),
                          c->RxDmaSrc, true);
            if (dma_setup_handshake(dma, &dc, true) != status_success) {
                s_tgtErr = 1u;
            }
            (void)i2c_slave_dma_transfer(p, (uint32_t)s_tgtLen);
        }
        i2c_clear_status(p, (uint32_t)I2C_EVENT_ADDRESS_HIT);
    }
    if ((st & (uint32_t)I2C_EVENT_TRANSACTION_COMPLETE) != 0u) {
        s_tgtDone = 1u;
        i2c_clear_status(p, (uint32_t)I2C_EVENT_TRANSACTION_COMPLETE);
    }
}

/* 防止 -Wunused-function（保留给 bring-up 诊断） */
void I2C_DebugDelay(void)
{
    i2c_status_poll_guard(1u);
}
