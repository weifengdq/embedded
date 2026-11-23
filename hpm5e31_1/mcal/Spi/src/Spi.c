/*
 * Spi.c —— AUTOSAR CP SPI Handler/Driver (模块 ID 83) 实现，HPM5E31/HPM5E31 SPI_Type
 *
 * 参考：AUTOSAR_CP_SWS_SPIHandlerDriver R25-11 §8.3（API）/§8.7（错误检查）/
 *       §9.4（EB 传输时序）；HPM5E31 RM "SPI" 章：
 *         TRANSFMT（数据位宽/CPOL/CPHA/主从）、TIMING（SCLK 分频/CS 建立保持）、
 *         TRANSCTRL（传输计数/传输模式）、STATUS（TXFULL/RXEMPTY/SPIACTIVE）、
 *         INTREN/INTRST（END 中断）、DATA（TX/RX FIFO 入口）。
 *       寄存器级已验证接线与 DMA 用法见 samples/spi/spi1_spi3_master_slave。
 *
 * 传输引擎（两条路径，同一个 Job/Sequence 状态机）：
 *   - CPU 泵：同步传输 / 异步轮询模式。逐字节写 DATA（TXFULL 等待）、读 DATA（RXEMPTY 等待）。
 *   - DMA：异步中断模式（主机 TX/RX DMA + END 中断）与从机（TX/RX DMA，TC 轮询）。
 * 从机 Job 的约定：Job 内只能有 1 个通道；武装后立刻推进到同序列的下一个 Job（主机发时钟），
 * 完成结果在主机传输结束后由 DMA TC 收集。该约定写进 README/章节。
 */
#include "Spi.h"
#include "Det.h"

#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_spi_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"

#if (SPI_DEV_ERROR_DETECT == STD_ON)
#define SPI_DET(api, err)   MCAL_DET_REPORT(SPI_MODULE_ID, 0u, (api), (err))
#else
#define SPI_DET(api, err)   ((void)0)
#endif

/* 运行状态 */
#define SPI_RUN_IDLE        (0u)
#define SPI_RUN_MASTER_SYNC (1u)   /* 同步：CPU 泵 */
#define SPI_RUN_MASTER_POLL (2u)   /* 异步轮询：CPU 泵，由 Spi_MainFunction_Handling 推进 */
#define SPI_RUN_MASTER_DMA  (3u)   /* 异步中断：DMA + END 中断 */
#define SPI_RUN_SLAVE       (4u)   /* 从机 DMA 已武装，等主机时钟 */
#define SPI_RUN_WAIT_SLAVE  (5u)   /* 主机已发完，等从机 DMA 收尾 */

typedef struct {
    uint8           seq;        /* 0xFF = 空闲 */
    uint8           jobPos;     /* 当前 Job 在 Sequence 里的下标 */
    uint8           chanPos;    /* 当前通道在 Job 里的下标 */
    uint8           state;      /* SPI_RUN_xxx */
    uint8           sync;       /* TRUE: Spi_SyncTransmit 发起的 */
    uint8           widthB;     /* 数据元素字节数 1/2/4 */
    uint8           fail;       /* 从机 DMA 出错标志 */
    Spi_HWUnitType  hw;
    Spi_JobType     job;
    const uint8    *txp;
    uint8          *rxp;
    const uint8    *txStart;
    uint8          *rxStart;
    uint32          elems;
    uint32          txRemain;
    uint32          rxRemain;
} Spi_RunStateType;

static const Spi_ConfigType *s_cfg = NULL;
static Spi_StatusType        s_status = SPI_UNINIT;
static Spi_SeqResultType     s_seqRes[SPI_SEQUENCE_COUNT];
static Spi_JobResultType     s_jobRes[SPI_JOB_COUNT];
static Spi_AsyncModeType     s_asyncMode = SPI_ASYNC_MODE_INTERRUPT;
static volatile Spi_RunStateType s_run;
static Spi_JobType           s_slavePendJob = 0xFFu;
static Spi_HWUnitType        s_slavePendHw  = 0xFFu;
static uint32                s_hwSclk[SPI_HW_UNIT_COUNT];
static uint32                s_hwSrcClk[SPI_HW_UNIT_COUNT];

/* EB 通道缓冲指针；IB 通道缓冲在驱动内部静态 RAM */
static const uint8 *s_ebSrc[SPI_CHANNEL_COUNT];
static uint8       *s_ebDst[SPI_CHANNEL_COUNT];
static uint16       s_ebLen[SPI_CHANNEL_COUNT];
/* IB 通道：TX/RX 必须分开两块缓冲（SWS 的 IB 只有一个逻辑缓冲，但硬件全双工下
 * TX 与 RX 同时访问同一块会互相覆盖，实测表现为"发什么收什么/全 0"）。 */
/* IB 缓冲必须放在 DMA 可访问的 non-cacheable RAM（HPM5E31 的 ILM/DLM 段 DMA 访问不到，
 * 实测放在默认 .bss 时 DMA 读写全 0）。EB 通道由调用者保证同样属性。 */
ATTR_PLACE_AT_NONCACHEABLE static Spi_DataBufferType s_ibTxBuf[SPI_CHANNEL_COUNT][SPI_IB_BUFFER_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static Spi_DataBufferType s_ibRxBuf[SPI_CHANNEL_COUNT][SPI_IB_BUFFER_SIZE];

/* ---------- 小工具 ---------- */
static SPI_Type *spi_base(Spi_HWUnitType hw)
{
    return (SPI_Type *)s_cfg->HWUnits[hw].Base;
}

static DMAV2_Type *spi_dma(Spi_HWUnitType hw)
{
    return (DMAV2_Type *)s_cfg->HWUnits[hw].DmaBase;
}

static DMAMUX_Type *spi_dmamux(Spi_HWUnitType hw)
{
    return (DMAMUX_Type *)s_cfg->HWUnits[hw].DmamuxBase;
}

static uint8 spi_width_bytes(Spi_ChannelType ch)
{
    return (uint8)((uint16)s_cfg->Channels[ch].DataWidth / 8u);
}

static uint8 spi_dma_width(uint8 widthB)
{
    if (widthB == 1u) { return (uint8)DMA_TRANSFER_WIDTH_BYTE; }
    if (widthB == 2u) { return (uint8)DMA_TRANSFER_WIDTH_HALF_WORD; }
    return (uint8)DMA_TRANSFER_WIDTH_WORD;
}

static void spi_chan_get_buffers(Spi_ChannelType ch, const uint8 **tx, uint8 **rx, uint16 *len)
{
    if (s_cfg->Channels[ch].UseInternalBuffer) {
        *tx  = s_ibTxBuf[ch];
        *rx  = s_ibRxBuf[ch];
        *len = s_cfg->Channels[ch].DefaultLength;
    } else {
        *tx  = s_ebSrc[ch];
        *rx  = s_ebDst[ch];
        *len = s_ebLen[ch];
    }
}

/* ---------- 主机控制配置：每次通道开始时重置 TRANSCTRL/FIFO ---------- */
static boolean spi_master_ctrl_start(Spi_HWUnitType hw, uint32 count, boolean dma)
{
    SPI_Type *p = spi_base(hw);
    spi_control_config_t ctrl;

    if (spi_set_data_bits(p, (uint8)(s_run.widthB * 8u)) != status_success) {
        return FALSE;
    }
    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable  = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.tx_dma_enable = dma;
    ctrl.common_config.rx_dma_enable = dma;
    ctrl.common_config.trans_mode    = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;

    if (dma) {
        return (spi_setup_dma_transfer(p, &ctrl, NULL, NULL, count, count) == status_success) ? TRUE : FALSE;
    }
    return (spi_control_init(p, &ctrl, count, count) == status_success) ? TRUE : FALSE;
}

/* ---------- 主机数据相：写/读 FIFO，寄存器级推进 ----------
 * STATUS.TXFULL / STATUS.RXEMPTY 是 SPI_Type 的硬件状态位；DATA 是 32 bit 入口，
 * 1/2/4 字节数据元素按小端打包（与 SDK hpm_spi_drv.c: spi_write_read_data 等价）。
 * 同步路径和异步轮询路径都走这个函数（异步轮询在 Spi_MainFunction_Handling 里调用），
 * 用泵函数而非死等，保证每次调用都有界。
 */
static boolean spi_master_blocking(void)
{
    SPI_Type *p = spi_base(s_run.hw);
    spi_control_config_t ctrl;
    hpm_stat_t st;

    /* 数据相用 SDK 的寄存器级 FIFO 泵（hpm_spi_drv.c: spi_transfer -> spi_write_read_data）：
     * 它把 TRANSCTRL/FIFO 复位/CMD 哑元/CS 时序一次配好。bring-up 阶段自研泵在
     * 主机侧出现"MISO 读回自己 MOSI"的异常，先降级到这里保证功能，章节"坑"里记录。 */
    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable  = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.tx_dma_enable = false;
    ctrl.common_config.rx_dma_enable = false;
    ctrl.common_config.trans_mode    = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;

    st = spi_transfer(p, &ctrl, NULL, NULL, (uint8_t *)s_run.txStart, s_run.elems,
                      s_run.rxStart, s_run.elems);
    return (st == status_success) ? TRUE : FALSE;
}

/* ---------- 从机 DMA 武装 ---------- */
static boolean spi_slave_arm(Spi_HWUnitType hw, const uint8 *txp, uint8 *rxp, uint32 count, uint8 widthB)
{
    const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[hw];
    SPI_Type *p = spi_base(hw);
    DMAV2_Type *dma = spi_dma(hw);
    DMAMUX_Type *mux = spi_dmamux(hw);
    dma_handshake_config_t c;
    spi_control_config_t ctrl;
    uint32_t tbytes = (uint32)((count + (uint32)h->ExtraTxElements) * (uint32)widthB);
    uint32_t rbytes = (uint32)(count * (uint32)widthB);

    spi_set_data_bits(p, (uint8)(widthB * 8u));

    /* 关键：上一次传输残留的 TXDMAEN/RXDMAEN 会让 DMA 在 spi_control_init 复位 FIFO 前
     * 抢先灌数据，被 FIFO 复位清掉但 DMA 已计数 => 数据整体丢头 N 字节。
     * 先关 DMA 使能，再武装通道，最后 spi_setup_dma_transfer 里统一打开。 */
    spi_disable_dma(p, (uint32_t)SPI_CTRL_TXDMAEN_MASK | (uint32_t)SPI_CTRL_RXDMAEN_MASK);

    /* 先武装 RX，再武装 TX（与已验证 SDK 例程一致：避免 TX 先请求把 FIFO 填乱） */
    dma_default_handshake_config(dma, &c);
    c.ch_index    = h->RxDmaChannel;
    c.src         = (uint32_t)&p->DATA;
    c.src_fixed   = true;
    c.dst         = (uint32_t)rxp;
    c.dst_fixed   = false;
    c.data_width  = spi_dma_width(widthB);
    c.size_in_byte = rbytes;
    dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, h->RxDmaChannel), h->RxDmaSrc, true);
    if (dma_setup_handshake(dma, &c, true) != status_success) {
        return FALSE;
    }

    /* TX：内存 -> SPI DATA */
    dma_default_handshake_config(dma, &c);
    c.ch_index    = h->TxDmaChannel;
    c.src         = (uint32_t)txp;
    c.src_fixed   = false;
    c.dst         = (uint32_t)&p->DATA;
    c.dst_fixed   = true;
    c.data_width  = spi_dma_width(widthB);
    c.size_in_byte = tbytes;
    dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, h->TxDmaChannel), h->TxDmaSrc, true);
    if (dma_setup_handshake(dma, &c, true) != status_success) {
        return FALSE;
    }

    spi_slave_get_default_control_config(&ctrl);
    ctrl.slave_config.slave_data_only = true;
    ctrl.common_config.tx_dma_enable = true;
    ctrl.common_config.rx_dma_enable = true;
    ctrl.common_config.trans_mode    = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;

    if (spi_setup_dma_transfer(p, &ctrl, NULL, NULL, count, count) != status_success) {
        return FALSE;
    }
    return TRUE;
}

static boolean spi_slave_check_done(Spi_HWUnitType hw)
{
    const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[hw];
    DMAV2_Type *dma = spi_dma(hw);
    uint32_t st_tx = dma_check_transfer_status(dma, h->TxDmaChannel);
    uint32_t st_rx = dma_check_transfer_status(dma, h->RxDmaChannel);

    if (((st_tx | st_rx) & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) != 0u) {
        s_run.fail = TRUE;
        return TRUE;
    }
    return (((st_tx & DMA_CHANNEL_STATUS_TC) != 0u) && ((st_rx & DMA_CHANNEL_STATUS_TC) != 0u)) ? TRUE : FALSE;
}

/* ---------- 主机 DMA 武装：异步中断模式 ---------- */
static boolean spi_master_dma_arm(Spi_HWUnitType hw, const uint8 *txp, uint8 *rxp, uint32 count, uint8 widthB)
{
    const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[hw];
    SPI_Type *p = spi_base(hw);
    DMAV2_Type *dma = spi_dma(hw);
    DMAMUX_Type *mux = spi_dmamux(hw);
    dma_handshake_config_t c;
    uint32_t bytes = (uint32)(count * (uint32)widthB);

    spi_disable_dma(p, (uint32_t)SPI_CTRL_TXDMAEN_MASK | (uint32_t)SPI_CTRL_RXDMAEN_MASK);

    dma_default_handshake_config(dma, &c);
    c.ch_index    = h->TxDmaChannel;
    c.src         = (uint32_t)txp;
    c.src_fixed   = false;
    c.dst         = (uint32_t)&p->DATA;
    c.dst_fixed   = true;
    c.data_width  = spi_dma_width(widthB);
    c.size_in_byte = bytes;
    dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, h->TxDmaChannel), h->TxDmaSrc, true);
    if (dma_setup_handshake(dma, &c, true) != status_success) {
        return FALSE;
    }

    dma_default_handshake_config(dma, &c);
    c.ch_index    = h->RxDmaChannel;
    c.src         = (uint32_t)&p->DATA;
    c.src_fixed   = true;
    c.dst         = (uint32_t)rxp;
    c.dst_fixed   = false;
    c.data_width  = spi_dma_width(widthB);
    c.size_in_byte = bytes;
    dmamux_config(mux, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(dma, h->RxDmaChannel), h->RxDmaSrc, true);
    if (dma_setup_handshake(dma, &c, true) != status_success) {
        return FALSE;
    }

    if (!spi_master_ctrl_start(hw, count, TRUE)) {
        return FALSE;
    }
    spi_clear_interrupt_status(p, 0xFFFFFFFFu);
    spi_enable_interrupt(p, (uint32_t)spi_end_int);
    return TRUE;
}

/* ---------- 主机 SCLK 切换（SpiBaudrate） ---------- */
static void spi_apply_sclk(Spi_HWUnitType hw, uint32 jobSclk)
{
    const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[hw];
    uint32 want = (jobSclk != 0u) ? jobSclk : h->SclkFreqHz;

    if (!h->IsMaster) {
        return;
    }
    if (want == s_hwSclk[hw]) {
        return;
    }
    {
        SPI_Type *p = spi_base(hw);
        spi_timing_config_t t;
        spi_master_get_default_timing_config(&t);
        t.master_config.clk_src_freq_in_hz = s_hwSrcClk[hw];
        t.master_config.sclk_freq_in_hz    = want;
        if (h->CsToSclk != 0u) { t.master_config.cs2sclk = h->CsToSclk; }
        if (h->CsHold != 0u)   { t.master_config.csht    = h->CsHold;   }
        if (spi_master_timing_init(p, &t) == status_success) {
            s_hwSclk[hw] = want;
        }
    }
}

/* ---------- 状态机 ---------- */
static void spi_seq_finish(Spi_SeqResultType result)
{
    Spi_SequenceType seq = (Spi_SequenceType)s_run.seq;

    s_seqRes[seq] = result;
    s_run.seq  = 0xFFu;
    s_run.state = SPI_RUN_IDLE;
    s_status = SPI_IDLE;
    if ((seq < SPI_SEQUENCE_COUNT) && (s_cfg->Sequences[seq].SeqEndNotification != NULL)) {
        s_cfg->Sequences[seq].SeqEndNotification();
    }
}

static void spi_job_finish(Spi_JobResultType result)
{
    Spi_JobType job = s_run.job;

    s_jobRes[job] = result;
    if (s_cfg->Jobs[job].JobEndNotification != NULL) {
        s_cfg->Jobs[job].JobEndNotification();
    }
}

static void spi_advance(void);

static void spi_chan_start(void)
{
    const Spi_JobConfigType *jc = &s_cfg->Jobs[s_run.job];
    Spi_ChannelType ch;
    const uint8 *tx = NULL;
    uint8 *rx = NULL;
    uint16 len = 0u;
    uint8 w;

    if (s_run.chanPos >= jc->ChannelCount) {
        spi_job_finish(SPI_JOB_OK);
        spi_advance();
        return;
    }
    ch = jc->ChannelList[s_run.chanPos];
    if ((ch >= SPI_CHANNEL_COUNT)) {
        SPI_DET((s_run.sync != 0u) ? SPI_API_ID_SYNC_TRANSMIT : SPI_API_ID_ASYNC_TRANSMIT, SPI_E_PARAM_CHANNEL);
        spi_job_finish(SPI_JOB_FAILED);
        spi_seq_finish(SPI_SEQ_FAILED);
        return;
    }
    spi_chan_get_buffers(ch, &tx, &rx, &len);
    w = spi_width_bytes(ch);
    if ((tx == NULL) || (rx == NULL) || (len == 0u) || (w == 0u)) {
        SPI_DET((s_run.sync != 0u) ? SPI_API_ID_SYNC_TRANSMIT : SPI_API_ID_ASYNC_TRANSMIT, SPI_E_PARAM_POINTER);
        spi_job_finish(SPI_JOB_FAILED);
        spi_seq_finish(SPI_SEQ_FAILED);
        return;
    }
    s_run.txp = tx;
    s_run.rxp = rx;
    s_run.txStart = tx;
    s_run.rxStart = rx;
    s_run.elems = (uint32)len;
    s_run.txRemain = (uint32)len;
    s_run.rxRemain = (uint32)len;
    s_run.widthB = w;
    s_run.fail = FALSE;

    if (s_cfg->HWUnits[s_run.hw].IsMaster) {
        spi_apply_sclk(s_run.hw, jc->SclkFreqHz);
        (void)spi_set_data_bits(spi_base(s_run.hw), (uint8)(w * 8u));
        if (s_run.sync != 0u) {
            s_run.state = SPI_RUN_MASTER_SYNC;
        } else if ((jc->AsyncMode == SPI_ASYNC_MODE_INTERRUPT) && (s_asyncMode == SPI_ASYNC_MODE_INTERRUPT)) {
            if (!spi_master_dma_arm(s_run.hw, tx, rx, (uint32)len, w)) {
                spi_job_finish(SPI_JOB_FAILED);
                spi_seq_finish(SPI_SEQ_FAILED);
                return;
            }
            s_run.state = SPI_RUN_MASTER_DMA;
        } else {
            s_run.state = SPI_RUN_MASTER_POLL;
        }
    } else {
        if (jc->ChannelCount != 1u) {
            spi_job_finish(SPI_JOB_FAILED);
            spi_seq_finish(SPI_SEQ_FAILED);
            return;
        }
        if (!spi_slave_arm(s_run.hw, tx, rx, (uint32)len, w)) {
            spi_job_finish(SPI_JOB_FAILED);
            spi_seq_finish(SPI_SEQ_FAILED);
            return;
        }
        s_slavePendJob = s_run.job;
        s_slavePendHw  = s_run.hw;
        s_jobRes[s_run.job] = SPI_JOB_PENDING;
        s_run.state = SPI_RUN_SLAVE;
        /* 从机已武装：立刻推进到下一个 Job（主机负责发时钟） */
        spi_advance();
    }
}

/* 当前通道传输完成（主机侧） */
static void spi_master_chan_done(boolean ok)
{
    const Spi_JobConfigType *jc = &s_cfg->Jobs[s_run.job];

    if (!ok) {
        spi_job_finish(SPI_JOB_FAILED);
        spi_seq_finish(SPI_SEQ_FAILED);
        return;
    }
    if (s_slavePendJob != 0xFFu) {
        /* 先收从机这一次；没收到就等（状态机下一拍再查） */
        if (!spi_slave_check_done(s_slavePendHw)) {
            s_run.state = SPI_RUN_WAIT_SLAVE;
            return;
        }
        s_jobRes[s_slavePendJob] = s_run.fail ? SPI_JOB_FAILED : SPI_JOB_OK;
        s_run.fail = FALSE;
        s_slavePendJob = 0xFFu;
        s_slavePendHw  = 0xFFu;
    }
    s_run.chanPos++;
    if (s_run.chanPos < jc->ChannelCount) {
        spi_chan_start();
    } else {
        spi_job_finish(SPI_JOB_OK);
        spi_advance();
    }
}

/* 当前 Job 结束：推进到序列下一个 Job 或结束序列 */
static void spi_advance(void)
{
    const Spi_SequenceConfigType *sc = &s_cfg->Sequences[s_run.seq];

    s_run.jobPos++;
    s_run.chanPos = 0u;
    if (s_run.jobPos >= sc->JobCount) {
        if (s_slavePendJob != 0xFFu) {
            s_run.state = SPI_RUN_WAIT_SLAVE;
            return;
        }
        spi_seq_finish(SPI_SEQ_OK);
        return;
    }
    s_run.job = sc->JobList[s_run.jobPos];
    s_run.hw  = s_cfg->Jobs[s_run.job].HwUnit;   /* 关键：Job 切换必须同步切换 HW Unit */
    spi_chan_start();
}

static void spi_seq_start(Spi_SequenceType seq, uint8 sync)
{
    const Spi_SequenceConfigType *sc = &s_cfg->Sequences[seq];

    s_run.seq     = (uint8)seq;
    s_run.jobPos  = 0u;
    s_run.chanPos = 0u;
    s_run.sync    = sync;
    s_run.fail    = FALSE;
    s_slavePendJob = 0xFFu;
    s_slavePendHw  = 0xFFu;
    s_seqRes[seq] = SPI_SEQ_PENDING;
    s_status = SPI_BUSY;

    if (sc->JobCount == 0u) {
        spi_seq_finish(SPI_SEQ_FAILED);
        return;
    }
    s_run.job = sc->JobList[0];
    s_run.hw  = s_cfg->Jobs[s_run.job].HwUnit;
    spi_chan_start();
}

/* 状态推进一拍：返回 TRUE 表示序列仍在进行 */
static boolean spi_pump(void)
{
    SPI_Type *p;

    if (s_run.seq == 0xFFu) {
        return FALSE;
    }
    switch (s_run.state) {
    case SPI_RUN_MASTER_SYNC:
    case SPI_RUN_MASTER_POLL:
        spi_master_chan_done(spi_master_blocking());
        break;
    case SPI_RUN_MASTER_DMA:
        p = spi_base(s_run.hw);
        if (!spi_is_active(p)) {
            const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[s_run.hw];
            DMAV2_Type *dma = spi_dma(s_run.hw);
            uint32_t st_tx = dma_check_transfer_status(dma, h->TxDmaChannel);
            uint32_t st_rx = dma_check_transfer_status(dma, h->RxDmaChannel);
            boolean ok = ((((st_tx | st_rx) & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) == 0u) &&
                          ((st_tx & DMA_CHANNEL_STATUS_TC) != 0u) && ((st_rx & DMA_CHANNEL_STATUS_TC) != 0u))
                         ? TRUE : FALSE;
            spi_disable_interrupt(p, 0xFFFFFFFFu);
            spi_master_chan_done(ok);
        }
        break;
    case SPI_RUN_SLAVE:
        /* 从机武装后状态机已推进，通常在 WAIT_SLAVE 里收尾；这里只在无后续 Job 时兜底 */
        if (spi_slave_check_done(s_slavePendHw)) {
            s_jobRes[s_slavePendJob] = s_run.fail ? SPI_JOB_FAILED : SPI_JOB_OK;
            s_run.fail = FALSE;
            s_slavePendJob = 0xFFu;
            s_slavePendHw  = 0xFFu;
            s_run.state = SPI_RUN_IDLE;
            spi_seq_finish(SPI_SEQ_OK);
        }
        break;
    case SPI_RUN_WAIT_SLAVE:
        if (spi_slave_check_done(s_slavePendHw)) {
            s_jobRes[s_slavePendJob] = s_run.fail ? SPI_JOB_FAILED : SPI_JOB_OK;
            s_run.fail = FALSE;
            s_slavePendJob = 0xFFu;
            s_slavePendHw  = 0xFFu;
            s_run.state = SPI_RUN_IDLE;
            /* 从机收尾后继续序列（可能还有后续 Job） */
            s_run.jobPos++;
            if (s_run.jobPos >= s_cfg->Sequences[s_run.seq].JobCount) {
                spi_seq_finish(SPI_SEQ_OK);
            } else {
                s_run.job = s_cfg->Sequences[s_run.seq].JobList[s_run.jobPos];
                s_run.hw  = s_cfg->Jobs[s_run.job].HwUnit;
                s_run.chanPos = 0u;
                spi_chan_start();
            }
        }
        break;
    default:
        break;
    }
    return (s_run.seq != 0xFFu) ? TRUE : FALSE;
}

/* ---------- SWS 8.3.1 Spi_Init ---------- */
void Spi_Init(const Spi_ConfigType *ConfigPtr)
{
    Spi_HWUnitType hw;

    if (s_status != SPI_UNINIT) {
        SPI_DET(SPI_API_ID_INIT, SPI_E_ALREADY_INITIALIZED);
        return;
    }
    if (ConfigPtr == NULL) {
        SPI_DET(SPI_API_ID_INIT, SPI_E_PARAM_POINTER);
        return;
    }
    s_cfg = ConfigPtr;

    for (hw = 0u; hw < SPI_HW_UNIT_COUNT; hw++) {
        const Spi_HWUnitConfigType *h = &s_cfg->HWUnits[hw];
        SPI_Type *p = (SPI_Type *)h->Base;
        spi_format_config_t  fmt;
        uint32 src_hz = 0u;

        if (h->PinInit != NULL) {
            h->PinInit();
        }
        if (h->ClockEnable != NULL) {
            src_hz = h->ClockEnable();
        }
        s_hwSrcClk[hw] = (src_hz != 0u) ? src_hz : MCAL_BOARD_XTAL_HZ;
        s_hwSclk[hw]   = 0u;

        if (h->IsMaster) {
            spi_master_get_default_format_config(&fmt);
            fmt.common_config.mode = spi_master_mode;
        } else {
            spi_slave_get_default_format_config(&fmt);
            fmt.common_config.mode = spi_slave_mode;
        }
        fmt.common_config.data_len_in_bits = 8u;
        fmt.common_config.cpol = (h->Cpol != 0u) ? spi_sclk_high_idle : spi_sclk_low_idle;
        fmt.common_config.cpha = (h->Cpha != 0u) ? spi_sclk_sampling_even_clk_edges
                                                : spi_sclk_sampling_odd_clk_edges;
        spi_format_init(p, &fmt);

        if (h->IsMaster) {
            spi_timing_config_t t;
            spi_master_get_default_timing_config(&t);
            t.master_config.clk_src_freq_in_hz = s_hwSrcClk[hw];
            t.master_config.sclk_freq_in_hz    = h->SclkFreqHz;
            if (h->CsToSclk != 0u) { t.master_config.cs2sclk = h->CsToSclk; }
            if (h->CsHold != 0u)   { t.master_config.csht    = h->CsHold;   }
            if (spi_master_timing_init(p, &t) == status_success) {
                s_hwSclk[hw] = h->SclkFreqHz;
            }
        }
        spi_disable_interrupt(p, 0xFFFFFFFFu);
        spi_clear_interrupt_status(p, 0xFFFFFFFFu);
        spi_set_tx_fifo_threshold(p, (SPI_SOC_FIFO_DEPTH / 2u));
        spi_set_rx_fifo_threshold(p, 1u);
    }

    for (hw = 0u; hw < SPI_CHANNEL_COUNT; hw++) {
        s_ebSrc[hw] = NULL;
        s_ebDst[hw] = NULL;
        s_ebLen[hw] = 0u;
    }
    for (hw = 0u; hw < SPI_SEQUENCE_COUNT; hw++) {
        s_seqRes[hw] = SPI_SEQ_OK;
    }
    for (hw = 0u; hw < SPI_JOB_COUNT; hw++) {
        s_jobRes[hw] = SPI_JOB_OK;
    }
    s_run.seq = 0xFFu;
    s_run.state = SPI_RUN_IDLE;
    s_slavePendJob = 0xFFu;
    s_slavePendHw  = 0xFFu;
    s_status = SPI_IDLE;
}

/* ---------- SWS 8.3.2 Spi_DeInit ---------- */
Std_ReturnType Spi_DeInit(void)
{
    Spi_HWUnitType hw;

    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_DEINIT, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    for (hw = 0u; hw < SPI_HW_UNIT_COUNT; hw++) {
        SPI_Type *p = spi_base(hw);
        spi_disable_interrupt(p, 0xFFFFFFFFu);
        spi_clear_interrupt_status(p, 0xFFFFFFFFu);
        spi_reset(p);
    }
    s_cfg = NULL;
    s_status = SPI_UNINIT;
    s_run.seq = 0xFFu;
    s_run.state = SPI_RUN_IDLE;
    return E_OK;
}

/* ---------- SWS 8.3.3 Spi_WriteIB ---------- */
Std_ReturnType Spi_WriteIB(Spi_ChannelType Channel, const Spi_DataBufferType *DataBufferPtr)
{
    uint16 i;

    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_WRITE_IB, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if (Channel >= SPI_CHANNEL_COUNT) {
        SPI_DET(SPI_API_ID_WRITE_IB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (!s_cfg->Channels[Channel].UseInternalBuffer) {
        SPI_DET(SPI_API_ID_WRITE_IB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (DataBufferPtr == NULL) {
        SPI_DET(SPI_API_ID_WRITE_IB, SPI_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    for (i = 0u; i < s_cfg->Channels[Channel].DefaultLength; i++) {
        s_ibTxBuf[Channel][i] = DataBufferPtr[i];
    }
    return E_OK;
}

/* ---------- SWS 8.3.4 Spi_ReadIB ---------- */
Std_ReturnType Spi_ReadIB(Spi_ChannelType Channel, Spi_DataBufferType *DataBufferPointer)
{
    uint16 i;

    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_READ_IB, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if (Channel >= SPI_CHANNEL_COUNT) {
        SPI_DET(SPI_API_ID_READ_IB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (!s_cfg->Channels[Channel].UseInternalBuffer) {
        SPI_DET(SPI_API_ID_READ_IB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (DataBufferPointer == NULL) {
        SPI_DET(SPI_API_ID_READ_IB, SPI_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    for (i = 0u; i < s_cfg->Channels[Channel].DefaultLength; i++) {
        DataBufferPointer[i] = s_ibRxBuf[Channel][i];
    }
    return E_OK;
}

/* ---------- SWS 8.3.6 Spi_SetupEB ---------- */
Std_ReturnType Spi_SetupEB(Spi_ChannelType Channel, const Spi_DataBufferType *SrcDataBufferPtr,
                           Spi_DataBufferType *DesDataBufferPtr, Spi_NumberOfDataType Length)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_SETUP_EB, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if (Channel >= SPI_CHANNEL_COUNT) {
        SPI_DET(SPI_API_ID_SETUP_EB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (s_cfg->Channels[Channel].UseInternalBuffer) {
        SPI_DET(SPI_API_ID_SETUP_EB, SPI_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if ((SrcDataBufferPtr == NULL) || (DesDataBufferPtr == NULL)) {
        SPI_DET(SPI_API_ID_SETUP_EB, SPI_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (Length == 0u) {
        SPI_DET(SPI_API_ID_SETUP_EB, SPI_E_PARAM_LENGTH);
        return E_NOT_OK;
    }
    s_ebSrc[Channel] = SrcDataBufferPtr;
    s_ebDst[Channel] = DesDataBufferPtr;
    s_ebLen[Channel] = Length;
    return E_OK;
}

/* ---------- SWS 8.3.7 Spi_AsyncTransmit ---------- */
Std_ReturnType Spi_AsyncTransmit(Spi_SequenceType Sequence)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_ASYNC_TRANSMIT, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if (Sequence >= SPI_SEQUENCE_COUNT) {
        SPI_DET(SPI_API_ID_ASYNC_TRANSMIT, SPI_E_PARAM_SEQ);
        return E_NOT_OK;
    }
    if (s_status == SPI_BUSY) {
        SPI_DET(SPI_API_ID_ASYNC_TRANSMIT, SPI_E_SEQ_PENDING);
        return E_NOT_OK;
    }
    spi_seq_start(Sequence, FALSE);
    /* 中断模式下 DMA/END 中断已经启动，通知由 ISR 触发；轮询模式由 MainFunction 推进 */
    return (s_status == SPI_BUSY) ? E_OK : E_NOT_OK;
}

/* ---------- SWS 8.3.11 Spi_SyncTransmit ---------- */
Std_ReturnType Spi_SyncTransmit(Spi_SequenceType Sequence)
{
    uint32 guard = SPI_TIMEOUT_POLLS;

    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_SYNC_TRANSMIT, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if (Sequence >= SPI_SEQUENCE_COUNT) {
        SPI_DET(SPI_API_ID_SYNC_TRANSMIT, SPI_E_PARAM_SEQ);
        return E_NOT_OK;
    }
    if (s_status == SPI_BUSY) {
        SPI_DET(SPI_API_ID_SYNC_TRANSMIT, SPI_E_SEQ_IN_PROCESS);
        return E_NOT_OK;
    }
    spi_seq_start(Sequence, TRUE);
    while ((s_run.seq != 0xFFu) && (guard-- != 0u)) {
        (void)spi_pump();
    }
    if (s_run.seq != 0xFFu) {
        /* 超时：强制收尾，避免总线挂死卡住上层 */
        s_run.seq = 0xFFu;
        s_run.state = SPI_RUN_IDLE;
        s_status = SPI_IDLE;
        s_seqRes[Sequence] = SPI_SEQ_FAILED;
        return E_NOT_OK;
    }
    return (s_seqRes[Sequence] == SPI_SEQ_OK) ? E_OK : E_NOT_OK;
}

/* ---------- SWS 8.3.8 Spi_GetStatus ---------- */
Spi_StatusType Spi_GetStatus(void)
{
    return s_status;
}

/* ---------- SWS 8.3.9 Spi_GetJobResult ---------- */
Spi_JobResultType Spi_GetJobResult(Spi_JobType Job)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_GET_JOB_RESULT, SPI_E_UNINIT);
        return SPI_JOB_FAILED;
    }
    if (Job >= SPI_JOB_COUNT) {
        SPI_DET(SPI_API_ID_GET_JOB_RESULT, SPI_E_PARAM_JOB);
        return SPI_JOB_FAILED;
    }
    return s_jobRes[Job];
}

/* ---------- SWS 8.3.10 Spi_GetSequenceResult ---------- */
Spi_SeqResultType Spi_GetSequenceResult(Spi_SequenceType Sequence)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_GET_SEQUENCE_RESULT, SPI_E_UNINIT);
        return SPI_SEQ_FAILED;
    }
    if (Sequence >= SPI_SEQUENCE_COUNT) {
        SPI_DET(SPI_API_ID_GET_SEQUENCE_RESULT, SPI_E_PARAM_SEQ);
        return SPI_SEQ_FAILED;
    }
    return s_seqRes[Sequence];
}

/* ---------- SWS 8.3.12 Spi_GetVersionInfo ---------- */
void Spi_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
    if (versioninfo == NULL) {
        SPI_DET(SPI_API_ID_GET_VERSION_INFO, SPI_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID         = (uint16)SPI_VENDOR_ID;
    versioninfo->moduleID         = (uint16)SPI_MODULE_ID;
    versioninfo->sw_major_version = (uint8)SPI_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)SPI_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)SPI_SW_PATCH_VERSION;
}

/* ---------- SWS 8.3.13 Spi_GetHWUnitStatus ---------- */
Spi_StatusType Spi_GetHWUnitStatus(Spi_HWUnitType HWUnit)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_GET_HW_UNIT_STATUS, SPI_E_UNINIT);
        return SPI_UNINIT;
    }
    if (HWUnit >= SPI_HW_UNIT_COUNT) {
        SPI_DET(SPI_API_ID_GET_HW_UNIT_STATUS, SPI_E_PARAM_UNIT);
        return SPI_UNINIT;
    }
    if (spi_is_active(spi_base(HWUnit))) {
        return SPI_BUSY;
    }
    return SPI_IDLE;
}

/* ---------- SWS 8.3.14 Spi_Cancel ---------- */
void Spi_Cancel(Spi_SequenceType Sequence)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_CANCEL, SPI_E_UNINIT);
        return;
    }
    if (Sequence >= SPI_SEQUENCE_COUNT) {
        SPI_DET(SPI_API_ID_CANCEL, SPI_E_PARAM_SEQ);
        return;
    }
    if ((s_run.seq == (uint8)Sequence) && (s_status == SPI_BUSY)) {
        Spi_HWUnitType hw;
        for (hw = 0u; hw < SPI_HW_UNIT_COUNT; hw++) {
            SPI_Type *p = spi_base(hw);
            spi_disable_interrupt(p, 0xFFFFFFFFu);
            spi_clear_interrupt_status(p, 0xFFFFFFFFu);
            spi_reset(p);
        }
        s_jobRes[s_run.job] = SPI_JOB_FAILED;
        s_seqRes[Sequence] = SPI_SEQ_CANCELED;
        s_run.seq = 0xFFu;
        s_run.state = SPI_RUN_IDLE;
    } else {
        s_seqRes[Sequence] = SPI_SEQ_CANCELED;
    }
    s_slavePendJob = 0xFFu;
    s_slavePendHw  = 0xFFu;
    s_status = SPI_IDLE;
}

/* ---------- SWS 8.3.15 Spi_SetAsyncMode ---------- */
Std_ReturnType Spi_SetAsyncMode(Spi_AsyncModeType Mode)
{
    if (s_status == SPI_UNINIT) {
        SPI_DET(SPI_API_ID_SET_ASYNC_MODE, SPI_E_UNINIT);
        return E_NOT_OK;
    }
    if ((Mode != SPI_ASYNC_MODE_INTERRUPT) && (Mode != SPI_ASYNC_MODE_POLLING)) {
        /* SWS R25-11 未给该参数定义 DevError 码，这里只返回 E_NOT_OK 并保持原模式 */
        return E_NOT_OK;
    }
    s_asyncMode = Mode;
    return E_OK;
}

/* ---------- SWS 8.5.1 Spi_MainFunction_Handling ---------- */
void Spi_MainFunction_Handling(void)
{
    uint32 guard = SPI_DMA_WAIT_POLLS;

    if (s_status != SPI_BUSY) {
        return;
    }
    while ((s_run.seq != 0xFFu) && (s_run.state != SPI_RUN_MASTER_DMA) && (guard-- != 0u)) {
        /* 轮询/同步模式在任务上下文推进；DMA 模式交给 ISR，避免和中断抢状态 */
        if (s_run.sync != 0u) {
            break;
        }
        if (!spi_pump()) {
            break;
        }
        if (s_run.state != SPI_RUN_MASTER_POLL) {
            break;
        }
    }
}

/* ---------- 中断入口（中断模式：DMA 完成 / END 中断） ---------- */
void Spi_Isr_HWUnit(Spi_HWUnitType HWUnit)
{
    SPI_Type *p;
    uint32_t st;

    if (s_cfg == NULL) {
        return;
    }
    if (HWUnit >= SPI_HW_UNIT_COUNT) {
        return;
    }
    p = spi_base(HWUnit);
    st = spi_get_interrupt_status(p);
    if (st == 0u) {
        return;
    }
    spi_clear_interrupt_status(p, st);

    if ((s_run.state == SPI_RUN_MASTER_DMA) && (s_run.hw == HWUnit)) {
        if ((st & (uint32_t)spi_end_int) != 0u) {
            spi_disable_interrupt(p, 0xFFFFFFFFu);
            (void)spi_pump();   /* 借 pump 的 MASTER_DMA 分支收尾 */
        }
    }
}
