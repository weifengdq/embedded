/*
 * Eth.c —— AUTOSAR Ethernet Driver（模块 ID 88）实现
 *
 * 依据：AUTOSAR_CP_SWS_EthernetDriver（R25-11）
 *       autosar_txt/AUTOSAR_CP_SWS_EthernetDriver.txt
 * 硬件：HPM5E31 ENET0（Synopsys DesignWare GMAC，RGMII 1 Gbit/s）
 *       HPM5E31 RM "ENET" 章（DMA 描述符 TDES/RDES 格式、MAC 寄存器、MDIO 帧格式）
 * 例程：samples/mcal/eth_raw_echo、eth_irq、eth_trcv
 * 文档：docs/chapters/50-mcal-eth.md
 *
 * 实现要点：
 *   1) DMA 环：TX/RX 各一组描述符数组，放 .noncacheable.bss 段（NOLOAD；第 13 章结论：描述符必须
 *      non-cacheable，否则 CPU 看到的 OWN 位与 DMA 不一致，表现为“偶发丢帧/重复发”)。
 *   2) 数据缓冲：放 .noncacheable.bss（AXI SRAM，NOLOAD，64 B cache line 对齐）；TX 提交前 clean、
 *      RX 取帧前 invalidate（ETH_TX/RX_CACHE_MAINTENANCE 可关，第 13 章有对照数据）。
 *   3) 描述符链初始化交给 SDK 的 enet_controller_init()（一次寄存器序列），运行期的
 *      OWN 位/长度/缓冲地址由本文件按 DesignWare TDES/RDES 格式操作。
 *   4) 双提交语义：PhysAddrPtr != NULL = SWS 的“驱动建头”；NULL = 整帧提交（lwIP 用）。
 */
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "Det.h"
#include "Mcal_Version.h"
#include "board.h"
#include "hpm_enet_drv.h"
#include "hpm_enet_regs.h"
#include "hpm_l1c_drv.h"
#include "hpm_csr_drv.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * 内部常量与类型
 * ======================================================================== */
#define ETH_CACHE_LINE_LEN   (64U)
#define ETH_RX_FCS_LEN       (4U)

/* ---- 描述符 / DMA 内存序屏障 ------------------------------------------------
 * 描述符与收发缓冲放在 non-cacheable 的 AXI SRAM（链接地址 0x1230000），而
 * DMA_TX_POLL_DEMAND / DMA_RX_POLL_DEMAND / DMA_STATUS 在 ENET 外设窗口（0xf1400000）。
 * RISC-V 对不同 PMA 区域的写没有顺序保证：写缓冲（store buffer）可以把"后写的
 * 外设寄存器"先送到 ENET。于是 DMA 在 OWN 位还没落到 SRAM 时就读到 poll demand，
 * 判定"没有新描述符"继续挂起 —— 已提交的帧要等下一次提交的 kick 才被一起发出去。
 * 实测现象（§50.9.12）：8 个 TX 缓冲攒满后同一微秒内一次性喷出 8 帧旧帧，
 * ping 只有 2/5~3/10 能对上 seq，TCP 建链的 SYN-ACK 被攒了 17 s、重发 8 份一起出去。
 * SDK 的 enet_prepare_tx_desc / ENET_TX_DESC_KICK_DMA 用同样的 fence w,w 与
 * fence w,o 规避（hpm_sdk/drivers/src/hpm_enet_drv.c:20-27），本驱动早期漏了这两条。
 *   ETH_FENCE_WW: 描述符字段（tbs1/buffer1/rdes2）先落地，OWN 最后对 DMA 可见
 *   ETH_FENCE_WO: 先前所有写（含 OWN）先落地，再写外设的 poll demand
 *   ETH_FENCE_RR: 读到 OWN==0 之后才读状态/数据（acquire），避免 CPU 把 buffer 读提前 */
#define ETH_FENCE_WW()   __asm volatile("fence w, w" ::: "memory")
#define ETH_FENCE_WO()   __asm volatile("fence w, o" ::: "memory")
#define ETH_FENCE_RR()   __asm volatile("fence r, r" ::: "memory")

/*! TX 缓冲状态机 */
typedef enum {
    ETH_TX_FREE      = 0,  /*!< 空闲，可被 Eth_ProvideTxBuffer 占用 */
    ETH_TX_LOCKED    = 1,  /*!< 已被 ProvideTxBuffer 锁定，等 Eth_Transmit */
    ETH_TX_SUBMITTED = 2   /*!< 已交给 DMA，等 txconf */
} Eth_TxStateType;

/*! 单控制器运行状态 */
typedef struct {
    ENET_Type                *Base;   /*!< 寄存器基址（来自 PBcfg 的 void*） */
    const Eth_CtrlConfigType *Cfg;    /*!< 该控制器的 post-build 配置 */
    Eth_ModeType              Mode;   /*!< 当前控制器模式 */
    uint8                     Mac[6]; /*!< 当前 MAC（网络字节序） */
    Eth_LinkInfoType          Link;   /*!< 当前链路参数（EthTrcv 联动或默认值） */

    volatile boolean RxPending;       /*!< ISR 置位：收到帧 */
    volatile uint32  RxIsrCount;      /*!< RX 中断进入次数 */
    volatile uint32  TxIsrCount;      /*!< TX 中断进入次数 */
    volatile uint32  LastIsrCycle;    /*!< 最近一次 RX 中断进入时的 mcycle */

    uint8          TxState[ETH_TX_BUF_COUNT];
    uint16         TxLen[ETH_TX_BUF_COUNT];
    boolean        TxConf[ETH_TX_BUF_COUNT];
    Eth_FrameType  TxType[ETH_TX_BUF_COUNT];

    boolean RxHeld[ETH_RX_BUF_COUNT];   /*!< TRUE=已交给上层，等 ReleaseRxBuffer */
    uint16  RxLen[ETH_RX_BUF_COUNT];
    uint16  RxIdx;                      /*!< 下一个待处理的 RX 描述符下标 */
    uint16  TxNext;                     /*!< 下一个待发放的 TX 描述符下标（与 DMA 环位置同步） */

    /* --- 统计（软件计数，SWS_Eth_91002/91003/91004 口径） --- */
    uint32 TxFrames, TxBytes, TxErrors, TxDroppedBusy, TxConfirmed;
    uint32 TxUnderflow, TxLateColl, TxDeferred;
    uint32 RxFrames, RxBytes, RxCrcErr, RxAlignErr, RxOverflow, RxErr, RxDropped;
    uint32 RxUndersize, RxOversize, RxBcast, RxMcast, RxUcast;
    uint32 RxSizeBucket[6];             /*!< 64 / 65-127 / 128-255 / 256-511 / 512-1023 / 1024-1518 */
    uint32 MiiErrors;
} Eth_DriverStateType;

/* ========================================================================
 * 驱动私有数据（描述符 non-cacheable，数据缓冲 cache line 对齐）
 * ======================================================================== */
static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(32)
       enet_tx_desc_t s_tx_desc[ETH_CTRL_COUNT][ETH_TX_BUF_COUNT];

static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(32)
       enet_rx_desc_t s_rx_desc[ETH_CTRL_COUNT][ETH_RX_BUF_COUNT];

/* 收发缓冲/描述符放 .noncacheable.bss（AXI SRAM，NOLOAD，不占 Flash 镜像）：
 *   1) ENET 的 DMA 只能访问 AXI/AHB SRAM，访问不到 core-local 的 ILM/DLM，
 *      若把缓冲放默认 .bss（DLM）会出现"描述符 OWN 一直不翻转、收不到帧"；
 *   2) 放 non-cacheable 后不需要每帧 clean/invalidate，省掉约 109 cyc/cacheline
 *      （第 13 章实测值）；把 ETH_TX/RX_CACHE_MAINTENANCE 打开可切回 cached 方案；
 *   3) 用 .noncacheable.bss 而非 .noncacheable：前者是 NOBITS/NOLOAD，段内容不进 .bin，
 *      startup 会按 __noncacheable_bss_start__/__end__ 清零；描述符另由 Eth_Init memset。 */
static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(ETH_CACHE_LINE_LEN)
       uint8 s_tx_buf[ETH_CTRL_COUNT][ETH_TX_BUF_COUNT][ETH_TX_BUF_SIZE];

static ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(ETH_CACHE_LINE_LEN)
       uint8 s_rx_buf[ETH_CTRL_COUNT][ETH_RX_BUF_COUNT][ETH_RX_BUF_SIZE];

static Eth_DriverStateType s_eth[ETH_CTRL_COUNT];
static boolean s_initialized = FALSE;
/*! MAC 过滤表槽位占用情况（槽 0 恒为自身地址；槽 1..3 供 UpdatePhysAddrFilter 用） */
static boolean s_filter_used[ETH_CTRL_COUNT][4];

/* ========================================================================
 * DET 便捷宏
 * ======================================================================== */
#define ETH_DET(ctrl, api, err)  MCAL_DET_REPORT(ETH_MODULE_ID, (uint8)(ctrl), (api), (err))

/* ========================================================================
 * 内部工具
 * ======================================================================== */
static uint32 eth_ptr32(const void *p)
{
    return (uint32) (unsigned long) p;
}

static uint32 eth_sys_addr(const void *p)
{
    return (uint32) core_local_mem_to_sys_address(BOARD_RUNNING_CORE, eth_ptr32(p));
}

#if (ETH_TX_CACHE_MAINTENANCE == STD_ON) || (ETH_RX_CACHE_MAINTENANCE == STD_ON)
static void eth_cache_clean(const void *p, uint32 len)
{
    uint32 a = eth_ptr32(p) & ~(ETH_CACHE_LINE_LEN - 1U);
    uint32 e = (eth_ptr32(p) + len + ETH_CACHE_LINE_LEN - 1U) & ~(ETH_CACHE_LINE_LEN - 1U);

    l1c_dc_writeback(a, e - a);
}

static void eth_cache_invalidate(const void *p, uint32 len)
{
    uint32 a = eth_ptr32(p) & ~(ETH_CACHE_LINE_LEN - 1U);
    uint32 e = (eth_ptr32(p) + len + ETH_CACHE_LINE_LEN - 1U) & ~(ETH_CACHE_LINE_LEN - 1U);

    l1c_dc_invalidate(a, e - a);
}
#endif /* cache maintenance */

/*! 取控制器状态并做索引/初始化检查；失败返回 NULL 并已上报 DET */
static Eth_DriverStateType *eth_state(uint8 CtrlIdx, uint8 ApiId)
{
    if (CtrlIdx >= ETH_CTRL_COUNT) {
        ETH_DET(CtrlIdx, ApiId, ETH_E_INV_CTRL_IDX);
        return NULL;
    }
    if (!s_initialized) {
        ETH_DET(CtrlIdx, ApiId, ETH_E_UNINIT);
        return NULL;
    }
    if (s_eth[CtrlIdx].Base == NULL) {
        ETH_DET(CtrlIdx, ApiId, ETH_E_UNINIT);
        return NULL;
    }
    return &s_eth[CtrlIdx];
}

/*! 按链路参数设置 MAC 的速率/双工（MACCFG.FES/DM，需在 TX/RX 停止时改） */
static void eth_apply_link(Eth_DriverStateType *st)
{
    enet_line_speed_t sp = enet_line_speed_1000mbps;

    if (st->Link.SpeedMbit <= 10U) {
        sp = enet_line_speed_10mbps;
    } else if (st->Link.SpeedMbit <= 100U) {
        sp = enet_line_speed_100mbps;
    }
    enet_set_line_speed(st->Base, sp);
    enet_set_duplex_mode(st->Base, st->Link.FullDuplex ? enet_full_duplex : enet_half_duplex);
}

/*! 把 RX 描述符 idx 重新挂回 DMA（清状态、设长度、置 OWN） */
static void eth_rx_arm(uint8 CtrlIdx, uint16 idx)
{
    Eth_DriverStateType *st = &s_eth[CtrlIdx];
    enet_rx_desc_t *d = &s_rx_desc[CtrlIdx][idx];
    uint8 *buf = &s_rx_buf[CtrlIdx][idx][0];

#if (ETH_RX_CACHE_MAINTENANCE == STD_ON)
    eth_cache_invalidate(buf, st->Cfg->RxBufLenByte);
#endif
    d->rdes0 = 0U;
    d->rdes1 = 0U;
    d->rdes1_bm.rch = 1U;
    d->rdes1_bm.rbs1 = st->Cfg->RxBufLenByte;
    d->rdes2_bm.buffer1 = eth_sys_addr(buf);
    ETH_FENCE_WW();                      /* rbs1/buffer1 先落地，OWN 最后对 DMA 可见 */
    d->rdes0_bm.own = 1U;
}

static void eth_rx_kick(Eth_DriverStateType *st)
{
    ETH_FENCE_WO();                      /* 先让描述符/缓冲的写对 DMA 可见，再踢 DMA */
    st->Base->DMA_RX_POLL_DEMAND = 1U;
}

static void eth_tx_kick(Eth_DriverStateType *st)
{
    ETH_FENCE_WO();
    st->Base->DMA_TX_POLL_DEMAND = 1U;
}

/*! 中断使能配置（按 PBcfg 的 RxInterruptEnable/TxInterruptEnable） */
static void eth_irq_config(Eth_DriverStateType *st)
{
    uint32 en = 0U;

#if (ETH_RX_INTERRUPT_SUPPORT == STD_ON)
    if (st->Cfg->RxInterruptEnable) {
        en |= ENET_DMA_INTR_EN_RIE_MASK;
    }
#endif
    if (st->Cfg->TxInterruptEnable) {
        en |= ENET_DMA_INTR_EN_TIE_MASK;
    }
    if (en != 0U) {
        en |= ENET_DMA_INTR_EN_NIE_MASK;   /* 中断汇总使能 */
    }
    st->Base->DMA_INTR_EN = en;
}

/*! 释放所有 TX 缓冲（SetControllerMode(DOWN) 用，SWS_Eth_00281） */
static void eth_tx_release_ctrl(uint8 CtrlIdx)
{
    Eth_DriverStateType *st = &s_eth[CtrlIdx];
    uint16 i;

    for (i = 0U; i < st->Cfg->TxBufCount; i++) {
        s_tx_desc[CtrlIdx][i].tdes0 = 0U;
        st->TxState[i] = (uint8) ETH_TX_FREE;
        st->TxLen[i] = 0U;
        st->TxConf[i] = FALSE;
    }
    /* SetControllerMode 从 DOWN 重新 ACTIVE 时，DMA 的当前描述符指针回到环首
     * （ST 0->1 会从 TX_DESC_LIST_ADDR 重新取），软件游标必须同步归零，
     * 否则发放顺序与 DMA 位置错位，见 Eth_ProvideTxBuffer 的说明。 */
    st->TxNext = 0U;
}

/*! 收回所有 RX 缓冲（SetControllerMode(DOWN) 用，SWS_Eth_00276） */
static void eth_rx_reclaim(uint8 CtrlIdx)
{
    Eth_DriverStateType *st = &s_eth[CtrlIdx];
    uint16 i;

    for (i = 0U; i < st->Cfg->RxBufCount; i++) {
        st->RxHeld[i] = FALSE;
        st->RxLen[i] = 0U;
        eth_rx_arm(CtrlIdx, i);
    }
    st->RxIdx = 0U;
    st->RxPending = FALSE;
}

/*! 帧长分桶（RMON etherStatsPktsNNOctets 口径，含 FCS） */
static void eth_rx_bucket(Eth_DriverStateType *st, uint32 len_with_fcs)
{
    if (len_with_fcs <= 64U) {
        st->RxSizeBucket[0]++;
    } else if (len_with_fcs <= 127U) {
        st->RxSizeBucket[1]++;
    } else if (len_with_fcs <= 255U) {
        st->RxSizeBucket[2]++;
    } else if (len_with_fcs <= 511U) {
        st->RxSizeBucket[3]++;
    } else if (len_with_fcs <= 1023U) {
        st->RxSizeBucket[4]++;
    } else {
        st->RxSizeBucket[5]++;
    }
}

/*! 解析目的 MAC 的单播/组播/广播属性 */
static void eth_rx_addr_class(Eth_DriverStateType *st, const uint8 *frame)
{
    if ((frame[0] & 0x01U) == 0U) {
        st->RxUcast++;
    } else if ((frame[0] & 0xFFU) == 0xFFU && (frame[1] & 0xFFU) == 0xFFU &&
               (frame[2] & 0xFFU) == 0xFFU && (frame[3] & 0xFFU) == 0xFFU &&
               (frame[4] & 0xFFU) == 0xFFU && (frame[5] & 0xFFU) == 0xFFU) {
        st->RxBcast++;
    } else {
        st->RxMcast++;
    }
}

/* ========================================================================
 * SWS API 实现
 * ======================================================================== */

/*! Eth_Init（SWS_Eth_00027 / Service ID 0x01） */
void Eth_Init(const Eth_ConfigType *CfgPtr)
{
    uint8 c;

    if ((CfgPtr == NULL) || (CfgPtr->CtrlConfigPtr == NULL) || (CfgPtr->CtrlCount == 0U) ||
        (CfgPtr->CtrlCount > ETH_CTRL_COUNT)) {
        ETH_DET(0U, ETH_API_ID_INIT, ETH_E_PARAM_POINTER);
        return;
    }

    for (c = 0U; c < CfgPtr->CtrlCount; c++) {
        const Eth_CtrlConfigType *cfg = &CfgPtr->CtrlConfigPtr[c];
        Eth_DriverStateType *st = &s_eth[c];
        ENET_Type *base = (ENET_Type *) cfg->BaseAddress;
        enet_desc_t desc;
        enet_mac_config_t mac_cfg;
        enet_int_config_t int_cfg;
        uint8 i;

        if (base == NULL) {
            ETH_DET(c, ETH_API_ID_INIT, ETH_E_PARAM_POINTER);
            return;
        }

        (void) memset(st, 0, sizeof(*st));
        st->Base = base;
        st->Cfg = cfg;
        st->Mode = ETH_MODE_DOWN;
        st->Link = cfg->DefaultLink;
        (void) memcpy(st->Mac, cfg->MacAddr, 6U);
        st->RxIdx = 0U;
        for (i = 0U; i < 4U; i++) {
            s_filter_used[c][i] = FALSE;
        }

        /* 1) 板级：RGMII 引脚复用、PHY 复位、MAC 侧时钟延迟、PLIC 使能 */
        Eth_BoardInit(c, cfg->BaseAddress);

        /* 2) 描述符表清零（缓冲内容不需要清） */
        (void) memset(s_tx_desc[c], 0, sizeof(s_tx_desc[c]));
        (void) memset(s_rx_desc[c], 0, sizeof(s_rx_desc[c]));

        /* 3) 组装 SDK 的 enet_desc_t：只用于控制器/DMA 寄存器初始化 */
        (void) memset(&desc, 0, sizeof(desc));
        desc.tx_desc_list_head = (enet_tx_desc_t *) eth_sys_addr(&s_tx_desc[c][0]);
        desc.rx_desc_list_head = (enet_rx_desc_t *) eth_sys_addr(&s_rx_desc[c][0]);
        desc.tx_buff_cfg.buffer = 0U;                    /* 0 = 零拷贝 TX，缓冲地址每帧填 */
        desc.tx_buff_cfg.count = cfg->TxBufCount;
        desc.tx_buff_cfg.size = cfg->TxBufLenByte;
        desc.rx_buff_cfg.buffer = eth_sys_addr(&s_rx_buf[c][0][0]);
        desc.rx_buff_cfg.count = cfg->RxBufCount;
        desc.rx_buff_cfg.size = cfg->RxBufLenByte;

        (void) memset(&mac_cfg, 0, sizeof(mac_cfg));
        mac_cfg.mac_addr_high[0] = ((uint32) st->Mac[5] << 8) | (uint32) st->Mac[4];
        mac_cfg.mac_addr_low[0] = ((uint32) st->Mac[3] << 24) | ((uint32) st->Mac[2] << 16) |
                                  ((uint32) st->Mac[1] << 8) | (uint32) st->Mac[0];
        mac_cfg.valid_max_count = 1U;
        mac_cfg.dma_pbl = cfg->DmaPbl;
        mac_cfg.sarc = enet_sarc_replace_mac0;

        (void) memset(&int_cfg, 0, sizeof(int_cfg));
        int_cfg.int_enable = 0U;
        int_cfg.int_mask = 0xFFFFFFFFU;

        if (enet_controller_init(base, enet_inf_rgmii, &desc, &mac_cfg, &int_cfg) != status_success) {
            ETH_DET(c, ETH_API_ID_INIT, ETH_E_INV_PARAM);
            return;
        }

        /* 4) 初始化后控制器停在 DOWN：清 DMA 的 ST/SR 位，等 SetControllerMode(ACTIVE) */
        base->DMA_OP_MODE &= ~(ENET_DMA_OP_MODE_ST_MASK | ENET_DMA_OP_MODE_SR_MASK);
        base->DMA_STATUS = base->DMA_STATUS;   /* 清掉初始化期间的挂起状态 */
        eth_irq_config(st);
        eth_apply_link(st);
        /* 只有配置里要求中断时才打开 PLIC，且必须在 DMA/MAC 初始化之后 */
        Eth_BoardIrqEnable(c, cfg->BaseAddress,
                           (boolean) ((cfg->RxInterruptEnable != FALSE) || (cfg->TxInterruptEnable != FALSE)));
        /* 清一次 MAC 的挂起中断（上电时可能残留 PHY/链路事件） */
        (void) base->INTR_STATUS;   /* INTR_STATUS 为只读，读即清挂起位 */
    }

    s_initialized = TRUE;
}

/*! Eth_SetControllerMode（SWS_Eth_91009 / 0x03） */
Std_ReturnType Eth_SetControllerMode(uint8 CtrlIdx, Eth_ModeType CtrlMode)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_SET_CONTROLLER_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((CtrlMode != ETH_MODE_ACTIVE) && (CtrlMode != ETH_MODE_DOWN)) {
        ETH_DET(CtrlIdx, ETH_API_ID_SET_CONTROLLER_MODE, ETH_E_INV_MODE);
        return E_NOT_OK;
    }
    if (CtrlMode == st->Mode) {
        return E_OK;
    }

    if (CtrlMode == ETH_MODE_DOWN) {
        /* SWS_Eth_00276：关 Tx/Rx、丢弃所有待发/待收请求 */
        st->Base->DMA_OP_MODE &= ~(ENET_DMA_OP_MODE_ST_MASK | ENET_DMA_OP_MODE_SR_MASK);
        eth_tx_release_ctrl(CtrlIdx);
        eth_rx_reclaim(CtrlIdx);
        st->Mode = ETH_MODE_DOWN;
    } else {
        /* SWS_Eth_00276：打开缓冲、激活 Tx/Rx */
        eth_apply_link(st);
        eth_tx_release_ctrl(CtrlIdx);
        eth_rx_reclaim(CtrlIdx);
        st->Base->DMA_OP_MODE |= (ENET_DMA_OP_MODE_ST_MASK | ENET_DMA_OP_MODE_SR_MASK);
        eth_tx_kick(st);
        eth_rx_kick(st);
        st->Mode = ETH_MODE_ACTIVE;
    }

#if (ETH_USE_ETHIF_CALLBACK == STD_ON)
    EthIf_CtrlModeIndication(CtrlIdx, st->Mode);
#endif
    return E_OK;
}

/*! Eth_GetControllerMode（SWS_Eth_91010 / 0x04） */
Std_ReturnType Eth_GetControllerMode(uint8 CtrlIdx, Eth_ModeType *CtrlModePtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_CONTROLLER_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (CtrlModePtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_CONTROLLER_MODE, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *CtrlModePtr = st->Mode;
    return E_OK;
}

/*! Eth_GetPhysAddr（SWS_Eth_00052 / 0x08） */
void Eth_GetPhysAddr(uint8 CtrlIdx, uint8 *PhysAddrPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_PHYS_ADDR);

    if (st == NULL) {
        return;
    }
    if (PhysAddrPtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_PHYS_ADDR, ETH_E_PARAM_POINTER);
        return;
    }
    (void) memcpy(PhysAddrPtr, st->Mac, 6U);
}

/*! Eth_SetPhysAddr（SWS_Eth_00151 / 0x13） */
void Eth_SetPhysAddr(uint8 CtrlIdx, const uint8 *PhysAddrPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_SET_PHYS_ADDR);

    if (st == NULL) {
        return;
    }
    if (PhysAddrPtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_SET_PHYS_ADDR, ETH_E_PARAM_POINTER);
        return;
    }
    (void) memcpy(st->Mac, PhysAddrPtr, 6U);
    /* MAC_ADDR_0：HIGH = addr[47:32]，LOW = addr[31:0]（RM "MAC Address 0 High/Low"） */
    st->Base->MAC_ADDR_0_HIGH = ((uint32) st->Mac[5] << 8) | (uint32) st->Mac[4];
    st->Base->MAC_ADDR_0_LOW = ((uint32) st->Mac[3] << 24) | ((uint32) st->Mac[2] << 16) |
                               ((uint32) st->Mac[1] << 8) | (uint32) st->Mac[0];
}

/*! Eth_UpdatePhysAddrFilter（SWS_Eth_00152 / 0x12）
 *  用 MAC_ADDR[1..3] 三个槽做精确匹配过滤（槽 0 是自身地址）；
 *  VlanId 参数本实现忽略（MAC 精确匹配，与 VLAN 无关），在文档“待确认项”里说明。 */
Std_ReturnType Eth_UpdatePhysAddrFilter(uint8 CtrlIdx, const uint8 *PhysAddrPtr,
                                        uint16 VlanId, Eth_FilterActionType Action)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_UPDATE_PHYS_ADDR_FILTER);
    uint8 slot;
    uint32 hi, lo;

    (void) VlanId;
    if (st == NULL) {
        return E_NOT_OK;
    }
    if (PhysAddrPtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_UPDATE_PHYS_ADDR_FILTER, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    hi = ((uint32) PhysAddrPtr[5] << 8) | (uint32) PhysAddrPtr[4];
    lo = ((uint32) PhysAddrPtr[3] << 24) | ((uint32) PhysAddrPtr[2] << 16) |
         ((uint32) PhysAddrPtr[1] << 8) | (uint32) PhysAddrPtr[0];

    if (Action == ETH_ADD_TO_FILTER) {
        for (slot = 1U; slot < 4U; slot++) {
            if (!s_filter_used[CtrlIdx][slot]) {
                s_filter_used[CtrlIdx][slot] = TRUE;
                st->Base->MAC_ADDR[slot - 1U].HIGH = 0x80000000UL | hi;  /* bit31 AE=1 */
                st->Base->MAC_ADDR[slot - 1U].LOW = lo;
                return E_OK;
            }
        }
        return E_NOT_OK;   /* 槽位用尽 */
    }

    /* ETH_REMOVE_FROM_FILTER：找到相同地址的槽并关掉 AE */
    for (slot = 1U; slot < 4U; slot++) {
        if (s_filter_used[CtrlIdx][slot] &&
            ((st->Base->MAC_ADDR[slot - 1U].HIGH & 0x7FFFFFFFUL) == hi) &&
            (st->Base->MAC_ADDR[slot - 1U].LOW == lo)) {
            s_filter_used[CtrlIdx][slot] = FALSE;
            st->Base->MAC_ADDR[slot - 1U].HIGH = 0U;
            st->Base->MAC_ADDR[slot - 1U].LOW = 0U;
            return E_OK;
        }
    }
    return E_NOT_OK;
}

/*! Eth_WriteMii（SWS_Eth_00062 / 0xA0）：Clause 22 写
 *  TrcvIdx 是“驱动内的收发器索引”，本板只有 1 颗 PHY（索引 0），映射到 PBcfg 的 PhyAddr */
Std_ReturnType Eth_WriteMii(uint8 CtrlIdx, uint8 TrcvIdx, uint8 RegIdx, uint16 RegVal)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_WRITE_MII);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (TrcvIdx != 0U) {
        ETH_DET(CtrlIdx, ETH_API_ID_WRITE_MII, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    if (enet_write_phy(st->Base, st->Cfg->PhyAddr, RegIdx, RegVal) != status_success) {
        st->MiiErrors++;
        return E_NOT_OK;
    }
    return E_OK;
}

/*! Eth_ReadMii（SWS_Eth_00069 / 0xA1）：Clause 22 读 */
Std_ReturnType Eth_ReadMii(uint8 CtrlIdx, uint8 TrcvIdx, uint8 RegIdx, uint16 *RegValPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_READ_MII);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((TrcvIdx != 0U) || (RegValPtr == NULL)) {
        ETH_DET(CtrlIdx, ETH_API_ID_READ_MII, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    if (enet_read_phy(st->Base, st->Cfg->PhyAddr, RegIdx, RegValPtr) != status_success) {
        st->MiiErrors++;
        return E_NOT_OK;
    }
    return E_OK;
}

/*! Eth_WriteMmd（SWS_Eth_00404 / 0xA2）：Clause 45 MMD 间接访问
 *  序列（YT8011 应用说明 §2.1.3）：0x0D=devad -> 0x0E=reg -> 0x0D=0x4000|devad -> 0x0E=data */
Std_ReturnType Eth_WriteMmd(uint8 CtrlIdx, uint8 TrcvIdx, uint8 Mmd, uint16 RegIdx, uint16 RegVal)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_WRITE_MMD);
    uint16 dummy = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (TrcvIdx != 0U) {
        ETH_DET(CtrlIdx, ETH_API_ID_WRITE_MMD, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    if ((Eth_WriteMii(CtrlIdx, 0U, 0x0DU, (uint16) Mmd) != E_OK) ||
        (Eth_WriteMii(CtrlIdx, 0U, 0x0EU, RegIdx) != E_OK) ||
        (Eth_WriteMii(CtrlIdx, 0U, 0x0DU, (uint16) (0x4000U | (uint16) Mmd)) != E_OK) ||
        (Eth_WriteMii(CtrlIdx, 0U, 0x0EU, RegVal) != E_OK)) {
        return E_NOT_OK;
    }
    (void) dummy;
    return E_OK;
}

/*! Eth_ReadMmd（SWS_Eth_00403 / 0xA3） */
Std_ReturnType Eth_ReadMmd(uint8 CtrlIdx, uint8 TrcvIdx, uint8 Mmd, uint16 RegIdx, uint16 *RegValPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_READ_MMD);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((TrcvIdx != 0U) || (RegValPtr == NULL)) {
        ETH_DET(CtrlIdx, ETH_API_ID_READ_MMD, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    if ((Eth_WriteMii(CtrlIdx, 0U, 0x0DU, (uint16) Mmd) != E_OK) ||
        (Eth_WriteMii(CtrlIdx, 0U, 0x0EU, RegIdx) != E_OK) ||
        (Eth_WriteMii(CtrlIdx, 0U, 0x0DU, (uint16) (0x4000U | (uint16) Mmd)) != E_OK) ||
        (Eth_ReadMii(CtrlIdx, 0U, 0x0EU, RegValPtr) != E_OK)) {
        return E_NOT_OK;
    }
    return E_OK;
}

/*! Eth_GetCounterValues（SWS_Eth_00231 / 0x14） */
Std_ReturnType Eth_GetCounterValues(uint8 CtrlIdx, Eth_CounterType *CounterPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_COUNTER_VALUES);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (CounterPtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_COUNTER_VALUES, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    (void) memset(CounterPtr, 0, sizeof(*CounterPtr));
    CounterPtr->DropPktBufOverrun = st->RxOverflow;
    CounterPtr->DropPktCrc        = st->RxCrcErr;
    CounterPtr->UndersizePkt      = st->RxUndersize;
    CounterPtr->OversizePkt       = st->RxOversize;
    CounterPtr->AlgnmtErr         = st->RxAlignErr;
    CounterPtr->DiscInbdPkt       = st->RxDropped;
    CounterPtr->ErrInbdPkt        = st->RxErr;
    CounterPtr->DiscOtbdPkt       = st->TxDroppedBusy;
    CounterPtr->ErrOtbdPkt        = st->TxErrors;
    CounterPtr->DfrdPkt           = st->TxDeferred;
    CounterPtr->LatCollPkt        = st->TxLateColl;
    CounterPtr->HwDepCtr0         = st->RxCrcErr;
    CounterPtr->HwDepCtr1         = st->RxAlignErr;
    CounterPtr->HwDepCtr2         = st->RxOverflow;
    CounterPtr->HwDepCtr3         = st->TxUnderflow;
    return E_OK;
}

/*! Eth_GetRxStats（SWS_Eth_91002 / 0x15） */
Std_ReturnType Eth_GetRxStats(uint8 CtrlIdx, Eth_RxStatsType *RxStats)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_RX_STATS);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (RxStats == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_RX_STATS, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    (void) memset(RxStats, 0, sizeof(*RxStats));
    RxStats->RxStatsDropEvents        = st->RxDropped;
    RxStats->RxStatsOctets            = st->RxBytes;
    RxStats->RxStatsPkts              = st->RxFrames;
    RxStats->RxStatsBroadcastPkts     = st->RxBcast;
    RxStats->RxStatsMulticastPkts     = st->RxMcast;
    RxStats->RxStatsCrcAlignErrors    = st->RxCrcErr + st->RxAlignErr;
    RxStats->RxStatsUndersizePkts     = st->RxUndersize;
    RxStats->RxStatsOversizePkts      = st->RxOversize;
    RxStats->RxStatsFragments         = st->RxUndersize;   /* 短帧+CRC 错 = 碎片 */
    RxStats->RxStatsJabbers           = st->RxOversize;
    RxStats->RxStatsCollisions        = 0U;                /* 全双工链路上恒 0 */
    RxStats->RxStatsPkts64Octets      = st->RxSizeBucket[0];
    RxStats->RxStatsPkts65to127Octets = st->RxSizeBucket[1];
    RxStats->RxStatsPkts128to255Octets = st->RxSizeBucket[2];
    RxStats->RxStatsPkts256to511Octets = st->RxSizeBucket[3];
    RxStats->RxStatsPkts512to1023Octets = st->RxSizeBucket[4];
    RxStats->RxStatsPkts1024to1518Octets = st->RxSizeBucket[5];
    RxStats->RxUnicastFrames          = st->RxUcast;
    return E_OK;
}

/*! Eth_GetTxStats（SWS_Eth_91003 / 0x1C） */
Std_ReturnType Eth_GetTxStats(uint8 CtrlIdx, Eth_TxStatsType *TxStats)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_TX_STATS);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (TxStats == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_TX_STATS, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    (void) memset(TxStats, 0, sizeof(*TxStats));
    TxStats->TxNumberOfOctets = st->TxBytes;
    TxStats->TxNUcastPkts     = 0U;
    TxStats->TxUniCastPkts    = st->TxFrames;
    return E_OK;
}

/*! Eth_GetTxErrorCounterValues（SWS_Eth_91004 / 0x1D） */
Std_ReturnType Eth_GetTxErrorCounterValues(uint8 CtrlIdx,
                                           Eth_TxErrorCounterValuesType *TxErrorCounterValues)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_TX_ERROR_COUNTER_VALUES);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (TxErrorCounterValues == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_TX_ERROR_COUNTER_VALUES, ETH_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    (void) memset(TxErrorCounterValues, 0, sizeof(*TxErrorCounterValues));
    TxErrorCounterValues->TxDroppedNoErrorPkts = st->TxDroppedBusy;
    TxErrorCounterValues->TxDroppedErrorPkts   = st->TxErrors;
    TxErrorCounterValues->TxDeferredTrans      = st->TxDeferred;
    TxErrorCounterValues->TxLateCollision      = st->TxLateColl;
    TxErrorCounterValues->TxUnderflowError     = st->TxUnderflow;
    return E_OK;
}

/*! Eth_ProvideTxBuffer（SWS_Eth_00077 / 0x09） */
BufReq_ReturnType Eth_ProvideTxBuffer(uint8 CtrlIdx, uint8 Priority, Eth_BufIdxType *BufIdxPtr,
                                      uint8 **BufPtr, uint16 *LenBytePtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_PROVIDE_TX_BUFFER);
    uint16 i;

    (void) Priority;   /* 单优先级队列：忽略优先级（SWS 允许多优先级，本实现 1 条） */
    if (st == NULL) {
        return BUFREQ_E_NOT_OK;
    }
    if ((BufIdxPtr == NULL) || (BufPtr == NULL) || (LenBytePtr == NULL)) {
        ETH_DET(CtrlIdx, ETH_API_ID_PROVIDE_TX_BUFFER, ETH_E_PARAM_POINTER);
        return BUFREQ_E_NOT_OK;
    }
    if (st->Mode != ETH_MODE_ACTIVE) {
        return BUFREQ_E_NOT_OK;
    }
    /* SWS_Eth_00079：请求超过可用长度时不锁定，返回可用长度 + BUFREQ_E_OVFL */
    if (*LenBytePtr > ETH_MAX_PAYLOAD) {
        *LenBytePtr = ETH_MAX_PAYLOAD;
        return BUFREQ_E_OVFL;
    }

    /* ---- 只发放"环游标 TxNext"指向的那一个缓冲（§50.9.12） ----------------
     * TX DMA 只能沿描述符链顺序前进，碰到 OWN==0 的描述符就进入 Suspend，
     * 靠 DMA_TX_POLL_DEMAND 唤醒；而唤醒后它仍旧从"它自己停住的那个位置"取描述符。
     * 早期实现按"下标最小的空闲缓冲"发放：一旦 TxConfirmation 把 DMA 已经走过的
     * 描述符（下标更小）标成空闲，上层就会把新帧塞到 DMA 身后 —— DMA 停在原地、
     * 每次 kick 都读到 OWN==0，已提交的帧全部积压，直到整环 8 个缓冲被填满、
     * 游标被迫回到 DMA 当前位置，才在同一微秒内把 6~8 帧一起喷出去（实测现象）。
     * 这里改成 TxNext 游标：发放 i 之后 TxNext=i+1，正好是 DMA 处理完 i 之后
     * 要取的描述符，两者永远同步；拿不到就按 SWS_Eth_00079 返回 BUFREQ_E_BUSY。 */
    i = st->TxNext;
    if (i >= st->Cfg->TxBufCount) {
        i = 0U;
    }
    if ((st->TxState[i] == (uint8) ETH_TX_FREE) &&
        (s_tx_desc[CtrlIdx][i].tdes0_bm.own == 0U)) {
        st->TxState[i] = (uint8) ETH_TX_LOCKED;
        st->TxConf[i] = FALSE;
        *BufIdxPtr = ETH_BUFIDX_MAKE(i);
        *BufPtr = &s_tx_buf[CtrlIdx][i][0];
        *LenBytePtr = ETH_MAX_PAYLOAD;
        return BUFREQ_OK;
    }
    st->TxDroppedBusy++;
    return BUFREQ_E_BUSY;
}

/*! Eth_Transmit（SWS_Eth_00087 / 0x0A） */
Std_ReturnType Eth_Transmit(uint8 CtrlIdx, Eth_BufIdxType BufIdx, Eth_FrameType FrameType,
                            boolean TxConfirmation, uint16 LenByte, const uint8 *PhysAddrPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_TRANSMIT);
    uint8 *buf;
    enet_tx_desc_t *d;
    uint16 idx;
    uint32 total;

    if (st == NULL) {
        return E_NOT_OK;
    }
    /* SWS_Eth_00129：非 ACTIVE 直接拒绝 */
    if (st->Mode != ETH_MODE_ACTIVE) {
        return E_NOT_OK;
    }
    if (!ETH_BUFIDX_IS_VALID(BufIdx)) {
        ETH_DET(CtrlIdx, ETH_API_ID_TRANSMIT, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    idx = ETH_BUFIDX_INDEX(BufIdx);
    if ((idx >= st->Cfg->TxBufCount) || (st->TxState[idx] != (uint8) ETH_TX_LOCKED)) {
        ETH_DET(CtrlIdx, ETH_API_ID_TRANSMIT, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    buf = &s_tx_buf[CtrlIdx][idx][0];

    if (PhysAddrPtr != NULL) {
        /* SWS 语义：驱动建 14 字节头，净荷由调用方写在 buf+14 */
        if (LenByte > ETH_MAX_PAYLOAD) {
            ETH_DET(CtrlIdx, ETH_API_ID_TRANSMIT, ETH_E_INV_PARAM);
            st->TxState[idx] = (uint8) ETH_TX_FREE;
            return E_NOT_OK;
        }
        (void) memcpy(&buf[0], PhysAddrPtr, 6U);            /* 目的 MAC */
        (void) memcpy(&buf[6], st->Mac, 6U);                /* 源 MAC */
        buf[12] = (uint8) (FrameType >> 8);
        buf[13] = (uint8) (FrameType & 0xFFU);
        total = (uint32) ETH_HEADER_LEN + (uint32) LenByte;
    } else {
        /* 扩展语义：整帧（含头）已在 buf，LenByte = 整帧长度 */
        total = (uint32) LenByte;
        if ((total < ETH_HEADER_LEN) || (total > ETH_MAX_FRAME_LEN)) {
            ETH_DET(CtrlIdx, ETH_API_ID_TRANSMIT, ETH_E_INV_PARAM);
            st->TxState[idx] = (uint8) ETH_TX_FREE;
            return E_NOT_OK;
        }
    }
    /* 补 0 到最小帧 60 字节（不含 FCS，MAC 自动补 FCS） */
    if (total < ETH_MIN_FRAME_LEN) {
        (void) memset(&buf[total], 0, (size_t) (ETH_MIN_FRAME_LEN - total));
        total = ETH_MIN_FRAME_LEN;
    }

#if (ETH_TX_CACHE_MAINTENANCE == STD_ON)
    eth_cache_clean(buf, total);
#endif

    d = &s_tx_desc[CtrlIdx][idx];
    if (d->tdes0_bm.own != 0U) {
        st->TxState[idx] = (uint8) ETH_TX_FREE;   /* 不留 LOCKED，否则该槽永久泄漏 */
        return E_NOT_OK;   /* DMA 还持有该描述符（理论上 ProvideTxBuffer 已过滤） */
    }
    d->tdes0 = 0U;
    d->tdes1 = 0U;
    d->tdes1_bm.tbs1 = (uint32) total;
    d->tdes2_bm.buffer1 = eth_sys_addr(buf);
    d->tdes0_bm.tch = 1U;                       /* 链式描述符 */
    if (TxConfirmation) {
        d->tdes0_bm.ic = 1U;                    /* 发送完成中断/状态 */
    }
    d->tdes0_bm.fs = 1U;
    d->tdes0_bm.ls = 1U;
    ETH_FENCE_WW();                             /* tbs1/buffer1/tch 先落地，OWN 必须最后写 */
    d->tdes0_bm.own = 1U;                       /* 交给 DMA 必须最后写 */

    st->TxState[idx] = (uint8) ETH_TX_SUBMITTED;
    st->TxNext = (uint16) ((idx + 1U) % st->Cfg->TxBufCount);   /* 与 DMA 位置保持同步 */
    st->TxConf[idx] = TxConfirmation;
    st->TxLen[idx] = (uint16) total;
    st->TxType[idx] = FrameType;
    st->TxFrames++;
    st->TxBytes += total;
    if ((buf[0] & 0x01U) == 0U) {
        /* 单播统计：TxUniCastPkts 已在 GetTxStats 里用 TxFrames 近似 */
    }
    eth_tx_kick(st);
    return E_OK;
}

/*! Eth_Receive（SWS_Eth_00095 / 0x0B）：只报状态，数据用 Eth_GetRxBuffer */
void Eth_Receive(uint8 CtrlIdx, uint8 QueueIdx, Eth_RxStatusType *RxStatusPtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_RECEIVE);
    uint16 i, idx, pending = 0U;

    if (st == NULL) {
        return;
    }
    if (RxStatusPtr == NULL) {
        ETH_DET(CtrlIdx, ETH_API_ID_RECEIVE, ETH_E_PARAM_POINTER);
        return;
    }
    if ((QueueIdx >= ETH_QUEUE_COUNT) || (st->Mode != ETH_MODE_ACTIVE)) {
        *RxStatusPtr = ETH_NOT_RECEIVED;
        return;
    }
    idx = st->RxIdx;
    for (i = 0U; i < st->Cfg->RxBufCount; i++) {
        uint16 k = (uint16) ((idx + i) % st->Cfg->RxBufCount);
        if ((s_rx_desc[CtrlIdx][k].rdes0_bm.own == 0U) && (!st->RxHeld[k])) {
            pending++;
        }
    }
    if (pending == 0U) {
        *RxStatusPtr = ETH_NOT_RECEIVED;
    } else if (pending == 1U) {
        *RxStatusPtr = ETH_RECEIVED;
    } else {
        *RxStatusPtr = ETH_RECEIVED_MORE_DATA_AVAILABLE;
    }
}

#if (ETH_EXT_API_ENABLE == STD_ON)
/*! Eth_GetRxBuffer（本实现扩展，0xE1）：零拷贝取一帧 */
Eth_RxStatusType Eth_GetRxBuffer(uint8 CtrlIdx, uint8 QueueIdx, Eth_BufIdxType *BufIdxPtr,
                                 const uint8 **DataPtr, uint16 *LenBytePtr,
                                 Eth_FrameType *FrameTypePtr)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_GET_RX_BUFFER);
    uint16 i;

    if (st == NULL) {
        return ETH_NOT_RECEIVED;
    }
    if ((BufIdxPtr == NULL) || (DataPtr == NULL) || (LenBytePtr == NULL) || (FrameTypePtr == NULL)) {
        ETH_DET(CtrlIdx, ETH_API_ID_GET_RX_BUFFER, ETH_E_PARAM_POINTER);
        return ETH_NOT_RECEIVED;
    }
    if ((QueueIdx >= ETH_QUEUE_COUNT) || (st->Mode != ETH_MODE_ACTIVE)) {
        return ETH_NOT_RECEIVED;
    }

    for (i = 0U; i < st->Cfg->RxPollBudget; i++) {
        enet_rx_desc_t *d = &s_rx_desc[CtrlIdx][st->RxIdx];
        uint32 len_fcs;
        uint16 payload;
        uint8 *buf;
        uint16 idx = st->RxIdx;

        if (d->rdes0_bm.own != 0U) {
            return ETH_NOT_RECEIVED;      /* DMA 还没填好 */
        }
        ETH_FENCE_RR();                   /* acquire：OWN==0 之后才读状态与数据 */
        buf = &s_rx_buf[CtrlIdx][idx][0];

        /* 错误帧：计数后直接重挂，不交给上层（RDES0 位定义见 hpm_enet_drv.h） */
        if ((d->rdes0_bm.es != 0U) || (d->rdes0_bm.fs == 0U) || (d->rdes0_bm.ls == 0U)) {
            if (d->rdes0_bm.ce != 0U) {
                st->RxCrcErr++;
            }
            if (d->rdes0_bm.oe != 0U) {
                st->RxOverflow++;
            }
            if (d->rdes0_bm.dse != 0U) {
                st->RxAlignErr++;
            }
            if (d->rdes0_bm.le != 0U) {
                st->RxUndersize++;
            }
            st->RxErr++;
            st->RxPending = FALSE;
            eth_rx_arm(CtrlIdx, idx);
            st->RxIdx = (uint16) ((st->RxIdx + 1U) % st->Cfg->RxBufCount);
            continue;
        }

        len_fcs = d->rdes0_bm.fl;                     /* RDES0.FL 含 FCS */
        payload = (len_fcs > ETH_RX_FCS_LEN) ? (uint16) (len_fcs - ETH_RX_FCS_LEN) : 0U;
        if (payload > st->Cfg->RxBufLenByte) {
            st->RxOversize++;
            st->RxErr++;
            eth_rx_arm(CtrlIdx, idx);
            st->RxIdx = (uint16) ((st->RxIdx + 1U) % st->Cfg->RxBufCount);
            continue;
        }
#if (ETH_RX_CACHE_MAINTENANCE == STD_ON)
        eth_cache_invalidate(buf, (uint32) payload);  /* 让 CPU 看到 DMA 写入的数据 */
#endif

        st->RxHeld[idx] = TRUE;
        st->RxLen[idx] = payload;
        st->RxFrames++;
        st->RxBytes += len_fcs;
        eth_rx_bucket(st, len_fcs);
        eth_rx_addr_class(st, buf);
        st->RxPending = FALSE;
        st->RxIdx = (uint16) ((st->RxIdx + 1U) % st->Cfg->RxBufCount);

        *BufIdxPtr = ETH_BUFIDX_MAKE(idx);
        *DataPtr = buf;
        *LenBytePtr = payload;
        *FrameTypePtr = (Eth_FrameType) (((uint16) buf[12] << 8) | (uint16) buf[13]);

        /* 还有没有下一帧？ */
        {
            enet_rx_desc_t *n = &s_rx_desc[CtrlIdx][st->RxIdx];
            return (n->rdes0_bm.own == 0U) ? ETH_RECEIVED_MORE_DATA_AVAILABLE : ETH_RECEIVED;
        }
    }
    return ETH_NOT_RECEIVED;
}
#endif /* ETH_EXT_API_ENABLE */

/*! Eth_ReleaseRxBuffer（SWS_Eth_91023 / 0x27 DRAFT）：归还接收缓冲 */
void Eth_ReleaseRxBuffer(uint8 CtrlIdx, Eth_BufIdxType RxHandleId)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_RELEASE_RX_BUFFER);
    uint16 idx;

    if (st == NULL) {
        return;
    }
    if (!ETH_BUFIDX_IS_VALID(RxHandleId)) {
        ETH_DET(CtrlIdx, ETH_API_ID_RELEASE_RX_BUFFER, ETH_E_RX_HANDLE_ID_NOT_ASSOCIATED);
        return;
    }
    idx = ETH_BUFIDX_INDEX(RxHandleId);
    if ((idx >= st->Cfg->RxBufCount) || (!st->RxHeld[idx])) {
        ETH_DET(CtrlIdx, ETH_API_ID_RELEASE_RX_BUFFER, ETH_E_RX_HANDLE_ID_NOT_ASSOCIATED);
        return;
    }
    st->RxHeld[idx] = FALSE;
    st->RxLen[idx] = 0U;
    eth_rx_arm(CtrlIdx, idx);

    /* RX 进程因无缓冲挂起时踢一脚 */
    if ((st->Base->DMA_STATUS & ENET_DMA_STATUS_RU_MASK) != 0U) {
        st->Base->DMA_STATUS = ENET_DMA_STATUS_RU_MASK;
        eth_rx_kick(st);
    }
}

/*! Eth_TxConfirmation（SWS_Eth_00100 / 0x0C） */
void Eth_TxConfirmation(uint8 CtrlIdx)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_TX_CONFIRMATION);
    uint16 i;

    if (st == NULL) {
        return;
    }
    if (st->Mode != ETH_MODE_ACTIVE) {
        return;
    }
    for (i = 0U; i < st->Cfg->TxBufCount; i++) {
        enet_tx_desc_t *d = &s_tx_desc[CtrlIdx][i];

        if ((st->TxState[i] != (uint8) ETH_TX_SUBMITTED) || (d->tdes0_bm.own != 0U)) {
            continue;
        }
        /* 错误位：ES 汇总；UF=下溢、LC=迟冲突、DF=推迟、EC=过多冲突 */
        if (d->tdes0_bm.es != 0U) {
            st->TxErrors++;
            if (d->tdes0_bm.uf != 0U) {
                st->TxUnderflow++;
            }
            if (d->tdes0_bm.lc != 0U) {
                st->TxLateColl++;
            }
            if (d->tdes0_bm.db != 0U) {
                st->TxDeferred++;
            }
        }
        if (st->TxConf[i]) {
            st->TxConfirmed++;
#if (ETH_USE_ETHIF_CALLBACK == STD_ON)
            EthIf_TxConfirmation(CtrlIdx, ETH_BUFIDX_MAKE(i), E_OK);
#endif
        }
        st->TxState[i] = (uint8) ETH_TX_FREE;
        st->TxLen[i] = 0U;
        st->TxConf[i] = FALSE;
    }
}

/*! Eth_MainFunction（SWS_Eth_00171 / 0x20） */
void Eth_MainFunction(void)
{
    uint8 c;

    if (!s_initialized) {
        return;
    }
    for (c = 0U; c < ETH_CTRL_COUNT; c++) {
        Eth_DriverStateType *st = &s_eth[c];

        if (st->Base == NULL) {
            continue;
        }
        Eth_TxConfirmation(c);

        /* 轮询模式下由这里消化 RX 中断挂起标志；中断模式下只清标志（数据由上层取） */
        if (st->RxPending) {
            st->RxPending = FALSE;
        }
        /* 丢帧检测（SWS_Eth_00169）：入口缓冲全部被上层占用 = 无法再收 */
        {
            uint16 i, held = 0U;
            for (i = 0U; i < st->Cfg->RxBufCount; i++) {
                if (st->RxHeld[i]) {
                    held++;
                }
            }
            if (held >= st->Cfg->RxBufCount) {
                st->RxDropped++;
            }
        }
    }
}

/*! Eth_GetVersionInfo（SWS_Eth_00106 / 0x0D） */
void Eth_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr)
{
    if (VersionInfoPtr == NULL) {
        ETH_DET(0U, ETH_API_ID_GET_VERSION_INFO, ETH_E_PARAM_POINTER);
        return;
    }
    VersionInfoPtr->vendorID = (uint16) MCAL_VENDOR_ID;
    VersionInfoPtr->moduleID = (uint16) ETH_MODULE_ID;
    VersionInfoPtr->sw_major_version = (uint8) ETH_SW_MAJOR_VERSION;
    VersionInfoPtr->sw_minor_version = (uint8) ETH_SW_MINOR_VERSION;
    VersionInfoPtr->sw_patch_version = (uint8) ETH_SW_PATCH_VERSION;
}

#if (ETH_EXT_API_ENABLE == STD_ON)
/*! Eth_SetLineSpeed（扩展，0xE2）：EthTrcv 链路联动用 */
Std_ReturnType Eth_SetLineSpeed(uint8 CtrlIdx, uint16 SpeedMbit, boolean FullDuplex)
{
    Eth_DriverStateType *st = eth_state(CtrlIdx, ETH_API_ID_SET_LINE_SPEED);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((SpeedMbit != 10U) && (SpeedMbit != 100U) && (SpeedMbit != 1000U)) {
        ETH_DET(CtrlIdx, ETH_API_ID_SET_LINE_SPEED, ETH_E_INV_PARAM);
        return E_NOT_OK;
    }
    st->Link.SpeedMbit = SpeedMbit;
    st->Link.FullDuplex = FullDuplex;
    if (st->Mode == ETH_MODE_ACTIVE) {
        eth_apply_link(st);
    }
    return E_OK;
}

/*! Eth_GetRxIsrCount（扩展，0xE3） */
uint32 Eth_GetRxIsrCount(uint8 CtrlIdx)
{
    if (CtrlIdx >= ETH_CTRL_COUNT) {
        return 0U;
    }
    return s_eth[CtrlIdx].RxIsrCount;
}

/*! Eth_DebugDump（扩展，调试用）：打印 MAC/DMA 关键寄存器与前 4 个描述符 */
void Eth_DebugDump(uint8 CtrlIdx)
{
    Eth_DriverStateType *st;
    uint16 i;

    if (CtrlIdx >= ETH_CTRL_COUNT) {
        return;
    }
    st = &s_eth[CtrlIdx];
    if (st->Base == NULL) {
        return;
    }
    (void) printf("REG MACCFG=0x%08X MACFF=0x%08X DMA_BUS=0x%08X DMA_OP=0x%08X\r\n",
                  (unsigned) st->Base->MACCFG, (unsigned) st->Base->MACFF,
                  (unsigned) st->Base->DMA_BUS_MODE, (unsigned) st->Base->DMA_OP_MODE);
    (void) printf("REG DMA_STATUS=0x%08X DMA_INTR_EN=0x%08X MACA0=0x%08X%04X\r\n",
                  (unsigned) st->Base->DMA_STATUS, (unsigned) st->Base->DMA_INTR_EN,
                  (unsigned) (st->Base->MAC_ADDR_0_HIGH & 0xFFFFU), (unsigned) st->Base->MAC_ADDR_0_LOW);
    for (i = 0U; (i < 4U) && (i < st->Cfg->TxBufCount); i++) {
        enet_tx_desc_t *d = &s_tx_desc[CtrlIdx][i];
        (void) printf("TD[%u] tdes0=0x%08X own=%u es=%u fs=%u ls=%u tbs1=%u buf=0x%08X state=%u\r\n",
                      (unsigned) i, (unsigned) d->tdes0, (unsigned) d->tdes0_bm.own,
                      (unsigned) d->tdes0_bm.es, (unsigned) d->tdes0_bm.fs,
                      (unsigned) d->tdes0_bm.ls, (unsigned) d->tdes1_bm.tbs1,
                      (unsigned) d->tdes2_bm.buffer1, (unsigned) st->TxState[i]);
    }
    /* TX 缓冲内容前 22 字节（核对帧头：目的MAC 6 + 源MAC 6 + 类型 2 + magic 4） */
    {
        const uint8 *b = &s_tx_buf[CtrlIdx][0][0];
        (void) printf("TXBUF0: %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X | %02X %02X | %02X %02X %02X %02X",
                      (unsigned) b[0], (unsigned) b[1], (unsigned) b[2], (unsigned) b[3], (unsigned) b[4], (unsigned) b[5],
                      (unsigned) b[6], (unsigned) b[7], (unsigned) b[8], (unsigned) b[9], (unsigned) b[10], (unsigned) b[11],
                      (unsigned) b[12], (unsigned) b[13], (unsigned) b[14], (unsigned) b[15], (unsigned) b[16], (unsigned) b[17]);
    }
    for (i = 0U; (i < 4U) && (i < st->Cfg->RxBufCount); i++) {
        enet_rx_desc_t *d = &s_rx_desc[CtrlIdx][i];
        (void) printf("RD[%u] rdes0=0x%08X own=%u fs=%u ls=%u fl=%u buf=0x%08X held=%u\r\n",
                      (unsigned) i, (unsigned) d->rdes0, (unsigned) d->rdes0_bm.own,
                      (unsigned) d->rdes0_bm.fs, (unsigned) d->rdes0_bm.ls,
                      (unsigned) d->rdes0_bm.fl, (unsigned) d->rdes2_bm.buffer1,
                      (unsigned) st->RxHeld[i]);
    }
}

/*! Eth_GetLastIsrCycle（扩展，0xE4）：最近一次 RX 中断进入的 mcycle */
uint32 Eth_GetLastIsrCycle(uint8 CtrlIdx)
{
    if (CtrlIdx >= ETH_CTRL_COUNT) {
        return 0U;
    }
    return s_eth[CtrlIdx].LastIsrCycle;
}
#endif /* ETH_EXT_API_ENABLE */

/*! 中断分发（Eth_PBcfg.c 的 Eth_Isr_0 调用） */
void Eth_Isr_Handler(uint8 CtrlIdx)
{
    Eth_DriverStateType *st;
    uint32 status;

    if (CtrlIdx >= ETH_CTRL_COUNT) {
        return;
    }
    st = &s_eth[CtrlIdx];
    if (st->Base == NULL) {
        return;
    }
    status = st->Base->DMA_STATUS;
    st->Base->DMA_STATUS = status;      /* W1C：写回清除 */
    (void) st->Base->INTR_STATUS;  /* MAC 侧挂起位：只读寄存器，读即清，避免中断风暴 */

    if ((status & ENET_DMA_STATUS_RI_MASK) != 0U) {
        st->RxIsrCount++;
        st->LastIsrCycle = (uint32) hpm_csr_get_core_mcycle();
        st->RxPending = TRUE;
    }
    if ((status & ENET_DMA_STATUS_TI_MASK) != 0U) {
        st->TxIsrCount++;
    }
    /* RX 缓冲不可用：踢一次轮询，让 DMA 重新取描述符 */
    if ((status & ENET_DMA_STATUS_RU_MASK) != 0U) {
        eth_rx_kick(st);
    }
}

#if (ETH_USE_ETHIF_CALLBACK == STD_ON)
/*! 弱定义：本仓库没有完整 EthIf，样例/集成层可覆盖 */
__attribute__((weak)) void EthIf_RxIndication(uint8 CtrlIdx, Eth_FrameType FrameType, boolean IsBroadcast,
                                             const uint8 *PhysAddrPtr, const uint8 *DataPtr, uint16 LenByte)
{
    (void) CtrlIdx; (void) FrameType; (void) IsBroadcast;
    (void) PhysAddrPtr; (void) DataPtr; (void) LenByte;
}

__attribute__((weak)) void EthIf_TxConfirmation(uint8 CtrlIdx, Eth_BufIdxType BufIdx, Std_ReturnType Result)
{
    (void) CtrlIdx; (void) BufIdx; (void) Result;
}

__attribute__((weak)) void EthIf_CtrlModeIndication(uint8 CtrlIdx, Eth_ModeType CtrlMode)
{
    (void) CtrlIdx; (void) CtrlMode;
}
#endif /* ETH_USE_ETHIF_CALLBACK */
