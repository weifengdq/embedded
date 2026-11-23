/*
 * Eth_PBcfg.c —— Eth 驱动 post-build 配置实例（契约 §5.3、§3.3）
 *
 * 本文件是“整块板子的以太网资源映射表”：ENET 实例、RGMII 引脚、PHY 复位脚、
 * RGMII 时钟延迟、PHY/MDIO 地址、默认 MAC、DMA 突发长度、缓冲规模、中断号与
 * 中断向量，全部集中在这里。驱动源码（src/Eth.c）里没有任何板级硬编码。
 *
 * 依据：
 *   - docs/MCAL开发约定.md §2 硬件接线（YT8011AN：RGMII PF02~PF13、MDC PA31/MDIO PA30、
 *     nINT PB28、nRST PB29；本轮起 YT8011AN/ENET0 归 Eth 负责人使用）
 *   - AUTOSAR_CP_SWS_EthernetDriver §10（EthConfigSet / EthCtrlConfig）
 *   - boards/hpm5e31_1/board.h 的 BOARD_ENET_RGMII_* 宏、board.c 的 board_init_enet_*()
 *   - HPM5E31 RM "ENET" 章；YT8011A_1.pdf §3 RESET/PHY Address/Strapping（p.38）
 */
#include "Eth_PBcfg.h"
#include "board.h"
#include "hpm_enet_drv.h"
#include "hpm_soc_irq.h"
#include "hpm_gpio_drv.h"

#define ETH_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "MemMap.h"

/* ========================================================================
 * 1) 控制器配置表（改板只改这里）
 *    ENET0 = RGMII + YT8011AN，PHY 地址 strap RXD[1:0]=01 -> 1
 * ======================================================================== */
static const Eth_CtrlConfigType s_eth_ctrl_cfg[ETH_CTRL_COUNT] = {
    {
        .CtrlIdx           = 0U,
        .BaseAddress       = (void *)HPM_ENET0,
        .IrqNumber         = (uint32) IRQn_ENET0,
        .PhyAddr           = 1U,                       /* YT8011AN MDIO 地址（strap） */
        /* 本地管理地址（02:...），避免与真实网卡冲突；量产应改从 OTP/配置取 */
        .MacAddr           = { 0x02U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U },
        .DmaPbl            = 32U,                      /* DMA 突发长度（AXI 拍数） */
        .TxBufCount        = ETH_TX_BUF_COUNT,
        .RxBufCount        = ETH_RX_BUF_COUNT,
        .TxBufLenByte      = ETH_TX_BUF_SIZE,
        .RxBufLenByte      = ETH_RX_BUF_SIZE,
        .RxInterruptEnable = FALSE,                    /* 默认轮询；eth_irq 例程置 TRUE */
        .TxInterruptEnable = FALSE,
        .RxChecksumOffload = FALSE,
        .RxPollBudget      = 16U,
        .DefaultLink       = { TRUE, 1000U, TRUE },    /* 未接 EthTrcv 时的固定链路 */
    },
};

/*! Eth 驱动 post-build 配置实例（SWS：const Eth_ConfigType Eth_Config） */
const Eth_ConfigType Eth_Config = {
    .CtrlConfigPtr = s_eth_ctrl_cfg,
    .CtrlCount     = ETH_CTRL_COUNT,
};

/* ========================================================================
 * 2) 板级初始化：引脚复用 / PHY 复位 / RGMII 时钟延迟 / 中断使能
 *    这些全部是“板子的事”，驱动只调本函数（契约 §3）。
 * ======================================================================== */
void Eth_BoardInit(uint8 CtrlIdx, void *BaseAddress)
{
    ENET_Type *base = (ENET_Type *) BaseAddress;

    if ((CtrlIdx >= ETH_CTRL_COUNT) || (base == NULL)) {
        return;
    }
    /* RGMII PF02~PF13 / MDC PA31 / MDIO PA30 引脚复用（boards/hpm5e31_1/board.c） */
    (void) board_init_enet_pins(base);
    /* PHY 硬复位：nRST PB29 拉低 30 ms（BOARD_ENET_RGMII_RST_DELAY_MS） */
    (void) board_reset_enet_phy(base);
    /* MAC 侧 RGMII 时钟延迟（board.h: BOARD_ENET_RGMII_TX_DLY/RX_DLY = 0） */
    (void) board_init_enet_rgmii_clock_delay(base);
    /* 注意：PLIC 的 ENET0 中断不在板级初始化里打开 —— MAC 的 INTR_STATUS 可能在上电时
     * 已有挂起位（PHY 唤醒/链路），先开 PLIC 会立刻进 ISR 风暴。改由 Eth_Init 在
     * DMA/MAC 配好、且配置里启用中断后再打开（见 Eth_BoardIrqEnable）。 */
}

/* ========================================================================
 * 3) 中断向量：ENET0 IRQ 是 RV32 机器模式外部中断，链接期绑定向量表槽位
 *    （与 mcal/Can/cfg/Can_PBcfg.c 的写法一致）
 * ======================================================================== */
void Eth_BoardIrqEnable(uint8 CtrlIdx, void *BaseAddress, boolean Enable)
{
    ENET_Type *base = (ENET_Type *) BaseAddress;

    (void) CtrlIdx;
    if (base == NULL) {
        return;
    }
    if (Enable) {
        (void) board_enable_enet_irq(base);
    } else {
        (void) board_disable_enet_irq(base);
    }
}

SDK_DECLARE_EXT_ISR_M(IRQn_ENET0, Eth_Isr_0)

void Eth_Isr_0(void)
{
    Eth_Isr_Handler(0U);
}
