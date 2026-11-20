/**
 * @file    s2500_regs.h
 * @brief   T30HM1TS2500 (S2500) 10BASE-T1S MAC-PHY register map
 *
 * 本文件按 T30HM1TS2500/D Rev.0 数据手册 "Register Memory Map" 章节整理。
 * 寄存器通过 OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface (OA-SPI)
 * 控制事务访问, 每个寄存器 32-bit, 由 (MMS, ADDR) 唯一寻址。
 *
 * 作者: S2500 适配工程
 * 许可: MIT
 */
#ifndef S2500_REGS_H
#define S2500_REGS_H

/* ---- Memory Map Selector (MMS) ---------------------------------- */
#define S2500_MMS_STD       0u   /* 32-bit 标准 SPI 控制/状态        */
#define S2500_MMS_MAC       1u   /* 32-bit MAC 控制/过滤/计数器      */
#define S2500_MMS_PCS       2u   /* 16-bit PCS (IEEE802.3 MMD3)     */
#define S2500_MMS_PMA       3u   /* 16-bit PMA/PCS (MMD1)           */
#define S2500_MMS_PLCA      4u   /* 16-bit PLCA / 厂商寄存器 (MMD31) */
#define S2500_MMS_VENDOR    12u  /* 32-bit onsemi 厂商专用寄存器     */

/* ================================================================= */
/* MMS 0 - Standard control and status                              */
/* ================================================================= */
#define S2500_REG_IDVER      0x0000u
#define S2500_REG_PHYID      0x0001u
#define S2500_REG_STDCAP     0x0002u
#define S2500_REG_RESET      0x0003u
#define S2500_REG_CONFIG0    0x0004u
#define S2500_REG_STATUS0    0x0008u
#define S2500_REG_STATUS1    0x0009u
#define S2500_REG_BUFSTS     0x000Bu
#define S2500_REG_IMSK0      0x000Cu
#define S2500_REG_IMSK1      0x000Du
#define S2500_REG_TTSCAH     0x0010u
#define S2500_REG_TTSCAL     0x0011u
#define S2500_REG_TTSCBH     0x0012u
#define S2500_REG_TTSCBL     0x0013u
#define S2500_REG_TTSCCH     0x0014u
#define S2500_REG_TTSCCL     0x0015u
#define S2500_REG_PHY_CTRL   0xFF00u
#define S2500_REG_PHY_STATUS 0xFF01u
#define S2500_REG_PHY_IDH    0xFF02u
#define S2500_REG_PHY_IDL    0xFF03u

/* CONFIG0 */
#define S2500_CONFIG0_SYNC          (1u << 15)
#define S2500_CONFIG0_TXFCSVE       (1u << 14)
#define S2500_CONFIG0_CSARFE        (1u << 13)
#define S2500_CONFIG0_ZARFE         (1u << 12)
#define S2500_CONFIG0_TXCTHRESH_1   (0u << 10)
#define S2500_CONFIG0_TXCTHRESH_4   (1u << 10)
#define S2500_CONFIG0_TXCTHRESH_8   (2u << 10)
#define S2500_CONFIG0_TXCTHRESH_16  (3u << 10)
#define S2500_CONFIG0_TXCTE         (1u << 9)
#define S2500_CONFIG0_RXCTE         (1u << 8)
#define S2500_CONFIG0_FTSE          (1u << 7)
#define S2500_CONFIG0_FTSS          (1u << 6)
#define S2500_CONFIG0_PROTE         (1u << 5)
#define S2500_CONFIG0_CPS_8         (3u << 0)
#define S2500_CONFIG0_CPS_16        (4u << 0)
#define S2500_CONFIG0_CPS_32        (5u << 0)
#define S2500_CONFIG0_CPS_64        (6u << 0)
#define S2500_CONFIG0_CPS_MASK      (7u << 0)

/* STATUS0 (写 1 清除的位, 除 PHYINT) */
#define S2500_STATUS0_CDPE          (1u << 12)
#define S2500_STATUS0_TXFCSE        (1u << 11)
#define S2500_STATUS0_TTSCAC        (1u << 10)
#define S2500_STATUS0_TTSCAB        (1u << 9)
#define S2500_STATUS0_TTSCAA        (1u << 8)
#define S2500_STATUS0_PHYINT        (1u << 7)
#define S2500_STATUS0_RESETC        (1u << 6)
#define S2500_STATUS0_HDRE          (1u << 5)
#define S2500_STATUS0_LOFE          (1u << 4)
#define S2500_STATUS0_RXBOE         (1u << 3)
#define S2500_STATUS0_TXBUE         (1u << 2)
#define S2500_STATUS0_TXBOE         (1u << 1)
#define S2500_STATUS0_TXPE          (1u << 0)
#define S2500_STATUS0_ERR_MASK      0x00001FBFu  /* CDPE..TXPE */

/* STATUS1 (写 1 清除) */
#define S2500_STATUS1_CMPD          (1u << 1)
#define S2500_STATUS1_CAPD          (1u << 0)

/* BUFSTS */
#define S2500_BUFSTS_TXC_SHIFT      8
#define S2500_BUFSTS_TXC_MASK       0x0000FF00u
#define S2500_BUFSTS_RCA_SHIFT      0
#define S2500_BUFSTS_RCA_MASK       0x000000FFu

/* IMSK0: 1 = 屏蔽该事件对 footer EXST 的影响 */
#define S2500_IMSK0_CDPEM           (1u << 12)
#define S2500_IMSK0_TXFCSEM         (1u << 11)
#define S2500_IMSK0_TTSCACM         (1u << 10)
#define S2500_IMSK0_TTSCABM         (1u << 9)
#define S2500_IMSK0_TTSCAAM         (1u << 8)
#define S2500_IMSK0_PHYINTM         (1u << 7)
#define S2500_IMSK0_HDREM           (1u << 5)
#define S2500_IMSK0_LOFEM           (1u << 4)
#define S2500_IMSK0_RXBOEM          (1u << 3)
#define S2500_IMSK0_TXBUEM          (1u << 2)
#define S2500_IMSK0_TXBOEM          (1u << 1)
#define S2500_IMSK0_TXPEM           (1u << 0)

/* IMSK1 */
#define S2500_IMSK1_CMPDM           (1u << 1)
#define S2500_IMSK1_CAPDM           (1u << 0)

/* PHY 控制/状态 (Clause 22) */
#define S2500_PHYCTRL_RESET         (1u << 15)
#define S2500_PHYCTRL_LOOPBACK      (1u << 14)
#define S2500_PHYCTRL_LCTL          (1u << 12)
#define S2500_PHYCTRL_LRST          (1u << 9)
#define S2500_PHYCTRL_CTEST         (1u << 7)

#define S2500_PHYSTS_S10M           (1u << 11)
#define S2500_PHYSTS_LNOK           (1u << 5)
#define S2500_PHYSTS_RJAB           (1u << 4)
#define S2500_PHYSTS_LKST           (1u << 2)
#define S2500_PHYSTS_LJAB           (1u << 1)

/* ================================================================= */
/* MMS 1 - MAC                                                      */
/* ================================================================= */
#define S2500_REG_MCONTROL0  0x0000u
#define S2500_REG_MADDRFILT0L 0x0010u
#define S2500_REG_MADDRFILT0H 0x0011u
#define S2500_REG_MADDRFILT1L 0x0012u
#define S2500_REG_MADDRFILT1H 0x0013u
#define S2500_REG_MADDRFILT2L 0x0014u
#define S2500_REG_MADDRFILT2H 0x0015u
#define S2500_REG_MADDRFILT3L 0x0016u
#define S2500_REG_MADDRFILT3H 0x0017u
#define S2500_REG_MADDRMASK0L 0x0020u
#define S2500_REG_MADDRMASK0H 0x0021u
#define S2500_REG_MADDRMASK1L 0x0022u
#define S2500_REG_MADDRMASK1H 0x0023u

/* 统计计数器基址: 0x30 .. 0x52, 每个 32-bit */
#define S2500_REG_MST_BASE    0x0030u
#define S2500_MST_TX_OCTETS_L 0x0030u
#define S2500_MST_TX_OCTETS_H 0x0031u
#define S2500_MST_TX_FRAMES   0x0032u
#define S2500_MST_TX_BCAST    0x0033u
#define S2500_MST_TX_MCAST    0x0034u
#define S2500_MST_TX_64       0x0035u
#define S2500_MST_TX_65_127   0x0036u
#define S2500_MST_TX_128_255  0x0037u
#define S2500_MST_TX_256_511  0x0038u
#define S2500_MST_TX_512_1023 0x0039u
#define S2500_MST_TX_1024     0x003Au
#define S2500_MST_TX_UNDERFLOW 0x003Bu
#define S2500_MST_TX_SINGLECOL 0x003Cu
#define S2500_MST_TX_MULTICOL 0x003Du
#define S2500_MST_TX_EXCESSCOL 0x003Eu
#define S2500_MST_TX_DEFERRED 0x003Fu
#define S2500_MST_TX_CRSERR   0x0040u
#define S2500_MST_RX_OCTETS_L 0x0041u
#define S2500_MST_RX_OCTETS_H 0x0042u
#define S2500_MST_RX_FRAMES   0x0043u
#define S2500_MST_RX_BCAST    0x0044u
#define S2500_MST_RX_MCAST    0x0045u
#define S2500_MST_RX_64       0x0046u
#define S2500_MST_RX_65_127   0x0047u
#define S2500_MST_RX_128_255  0x0048u
#define S2500_MST_RX_256_511  0x0049u
#define S2500_MST_RX_512_1023 0x004Au
#define S2500_MST_RX_1024     0x004Bu
#define S2500_MST_RX_RUNTS    0x004Cu
#define S2500_MST_RX_TOOLONG  0x004Du
#define S2500_MST_RX_FCSERR   0x004Eu
#define S2500_MST_RX_SYMBOLERR 0x004Fu
#define S2500_MST_RX_ALIGNERR 0x0050u
#define S2500_MST_RX_OVERFLOW 0x0051u
#define S2500_MST_RX_DROPPED  0x0052u
#define S2500_MST_LAST        S2500_MST_RX_DROPPED

/* MCONTROL0 */
#define S2500_MC_IPGNF        (1u << 21)
#define S2500_MC_BKOD         (1u << 20)
#define S2500_MC_NFCSF        (1u << 19)
#define S2500_MC_MCSF         (1u << 18)
#define S2500_MC_BCSF         (1u << 17)
#define S2500_MC_ADRF         (1u << 16)
#define S2500_MC_FCSA         (1u << 8)
#define S2500_MC_TXEN         (1u << 1)
#define S2500_MC_RXEN         (1u << 0)

/* ================================================================= */
/* MMS 2 - PCS                                                      */
/* ================================================================= */
#define S2500_REG_PCSCTL     0x08F3u
#define S2500_REG_PCSSTATUS  0x08F4u
#define S2500_REG_PCSDIAG1   0x08F5u   /* REMJAB 计数, 读清 */
#define S2500_REG_PCSDIAG2   0x08F6u   /* 物理碰撞计数, 读清 */

#define S2500_PCSCTL_RST     (1u << 15)
#define S2500_PCSCTL_LOOP    (1u << 14)
#define S2500_PCSSTATUS_JAB  (1u << 7)

/* ================================================================= */
/* MMS 3 - PMA                                                      */
/* ================================================================= */
#define S2500_REG_DEVINPKG1  0x0005u
#define S2500_REG_DEVINPKG2  0x0006u
#define S2500_REG_PMAPMDEXT  0x000Bu
#define S2500_REG_BASET1EXT  0x0012u
#define S2500_REG_PMACTL     0x08F9u
#define S2500_REG_PMASTATUS  0x08FAu
#define S2500_REG_PMATEST    0x08FBu

#define S2500_PMACTL_RST     (1u << 15)
#define S2500_PMACTL_TXDIS   (1u << 14)
#define S2500_PMACTL_MULT    (1u << 10)
#define S2500_PMACTL_LOOP    (1u << 0)

/* ================================================================= */
/* MMS 4 - PLCA / 诊断 / TC10 / TD / 厂商                           */
/* ================================================================= */
#define S2500_REG_PLCAIDVER  0xCA00u
#define S2500_REG_PLCACTL0   0xCA01u
#define S2500_REG_PLCACTL1   0xCA02u
#define S2500_REG_PLCASTATUS 0xCA03u
#define S2500_REG_PLCATOTMR  0xCA04u
#define S2500_REG_PLCABURST  0xCA05u
#define S2500_REG_PLCADIAG   0xCA06u

#define S2500_PLCA_EN        (1u << 15)
#define S2500_PLCA_RST       (1u << 14)
#define S2500_PLCA_NCNT_SHIFT 8
#define S2500_PLCA_NCNT_MASK (0xFFu << 8)
#define S2500_PLCA_ID_MASK   0xFFu
#define S2500_PLCA_PST       (1u << 15)

#define S2500_PLCADIAG_RXINTO   (1u << 2)
#define S2500_PLCADIAG_UNEXPB   (1u << 1)
#define S2500_PLCADIAG_BCNBFTO  (1u << 0)

/* 拓扑发现 */
#define S2500_REG_TD_CTRL        0xCE00u
#define S2500_REG_TD_STAT        0xCE01u
#define S2500_REG_TD_DIST_LOW    0xCE02u
#define S2500_REG_TD_DIST_HIGH   0xCE03u
#define S2500_REG_TD_DLY_LOW     0xCE04u
#define S2500_REG_TD_DLY_HIGH    0xCE05u
#define S2500_REG_TD_MNDLY_LOW   0xCE06u
#define S2500_REG_TD_MNDLY_HIGH  0xCE07u
#define S2500_REG_TD_MNDLY_DUR   0xCE08u

#define S2500_TDCTRL_EN          (1u << 15)
#define S2500_TDCTRL_REFN        (1u << 14)
#define S2500_TDCTRL_DLYM_START  (1u << 13)
#define S2500_TDCTRL_DM_DUR_SHIFT 9
#define S2500_TDCTRL_DM_DUR_MASK (0xFu << 9)
#define S2500_TDCTRL_DM_START    (1u << 8)
#define S2500_TDCTRL_AUTO_START  (1u << 7)

#define S2500_TDSTAT_DLYM_DONE   (1u << 15)
#define S2500_TDSTAT_DLYM_ERR    (1u << 14)
#define S2500_TDSTAT_DM_DONE     (1u << 13)
#define S2500_TDSTAT_DM_ERR      (1u << 12)
#define S2500_TDSTAT_AUTO_ERR    (1u << 11)

/* TC10 睡眠/唤醒 */
#define S2500_REG_WS_STATUS0     0xD000u
#define S2500_REG_WS_CTRL0       0xD001u
#define S2500_REG_WS_STATUS1     0xD100u
#define S2500_REG_WS_CTRL1       0xD101u

#define S2500_WS0_LPCAP          (1u << 15)
#define S2500_WS0_LP_FAIL        (1u << 14)
#define S2500_WSCTRL0_LPREQ      (1u << 15)
#define S2500_WSCTRL0_LPEXIT     (1u << 14)

#define S2500_WS1_POR_VCC        (1u << 15)
#define S2500_WS1_POR_VIO        (1u << 14)
#define S2500_WS1_RST_IN         (1u << 12)
#define S2500_WS1_TSD            (1u << 9)
#define S2500_WS1_WAKE_WUT       (1u << 8)
#define S2500_WS1_WAKE_PIN       (1u << 7)
#define S2500_WS1_LP_FAIL        (1u << 0)

#define S2500_WSCTRL1_WFWD       (1u << 15)
#define S2500_WSCTRL1_WDEB       (1u << 14)
#define S2500_WSCTRL1_LWD        (1u << 13)
#define S2500_WSCTRL1_RWD        (1u << 12)
#define S2500_WSCTRL1_RWCD       (1u << 11)
#define S2500_WSCTRL1_WOUT       (1u << 10)
#define S2500_WSCTRL1_LWTH       (1u << 9)
#define S2500_WSCTRL1_RWTH       (1u << 8)
#define S2500_WSCTRL1_LPRQ       (1u << 1)
#define S2500_WSCTRL1_GW         (1u << 0)

/* 高级诊断: HDD / SQI */
#define S2500_REG_ADFCAP         0xCC00u
#define S2500_REG_HDD            0xCC01u
#define S2500_REG_DCQ_TOID       0xCC02u
#define S2500_REG_DCQ_SQI        0xCC03u
#define S2500_REG_DCQ_SQIP       0xCC04u
#define S2500_REG_HDD_TUNE1      0xCD00u
#define S2500_REG_HDD_TUNE2      0xCD01u
#define S2500_REG_HDD_TUNE3      0xCD02u

#define S2500_ADFCAP_HDD1W       (1u << 11)
#define S2500_ADFCAP_HDD_SHIFT   8
#define S2500_ADFCAP_HDD_MASK    (0x7u << 8)
#define S2500_ADFCAP_SQIP_SHIFT  1
#define S2500_ADFCAP_SQI         (1u << 0)

#define S2500_HDD_CTRL           (1u << 15)
#define S2500_HDD_READY          (1u << 14)
#define S2500_HDD_START          (1u << 13)
#define S2500_HDD_1W_ST          (1u << 3)
#define S2500_HDD_VALID          (1u << 2)
#define S2500_HDD_SHORT_OPEN_MASK 0x3u

#define S2500_SQI_UPD            (1u << 15)
#define S2500_SQI_MASK           0x7u
#define S2500_SQIP_UPD           (1u << 15)
#define S2500_SQIP_MASK          0xFFu

/* 厂商专用 (MMS4) */
#define S2500_REG_ONIPVER        0x8000u
#define S2500_REG_TWEAKS1        0x8001u
#define S2500_REG_PLCAEXT        0x8002u
#define S2500_REG_PMATUNE0       0x8003u
#define S2500_REG_PMATUNE1       0x8004u
#define S2500_REG_WUPTUNE        0x8007u

#define S2500_TWEAKS1_ENIE       (1u << 7)
#define S2500_TWEAKS1_UNJT       (1u << 6)
#define S2500_TWEAKS1_SCRD       (1u << 2)
#define S2500_TWEAKS1_ECOLE      (1u << 1)

#define S2500_PLCAEXT_PRECEDENCE (1u << 15)
#define S2500_PLCAEXT_CRPS       (1u << 4)
#define S2500_PLCAEXT_CREN       (1u << 2)
#define S2500_PLCAEXT_LDEN       (1u << 1)
#define S2500_PLCAEXT_LDR        (1u << 0)

/* ================================================================= */
/* MMS 12 - onsemi 厂商寄存器                                       */
/* ================================================================= */
#define S2500_REG_PHYIRQM        0x0010u
#define S2500_REG_PHYIRQS        0x0011u
#define S2500_REG_DIOCFG0        0x0012u
#define S2500_REG_ONICVER        0x1000u
#define S2500_REG_TWEAKS2        0x1001u
#define S2500_REG_MACID0         0x1002u
#define S2500_REG_MACID1         0x1003u
#define S2500_REG_NVM_HEALTH     0x1005u
#define S2500_REG_PTPSEC         0x1010u
#define S2500_REG_PTPNSEC        0x1011u
#define S2500_REG_SETSEC         0x1012u
#define S2500_REG_SETNSEC        0x1013u
#define S2500_REG_PTPADJ         0x1014u
#define S2500_REG_CMPCTL         0x1015u
#define S2500_REG_CMPSEC         0x1016u
#define S2500_REG_CMPNSEC        0x1017u
#define S2500_REG_CMPPER         0x1018u
#define S2500_REG_CAPCTL         0x1019u
#define S2500_REG_CAPSEC         0x101Au
#define S2500_REG_CAPNSEC        0x101Bu
#define S2500_REG_BCNCNT         0x101Cu

#define S2500_IRQ_CRDI           (1u << 6)
#define S2500_IRQ_PCI            (1u << 5)
#define S2500_IRQ_PRI            (1u << 4)
#define S2500_IRQ_RJI            (1u << 3)
#define S2500_IRQ_LJI            (1u << 2)
#define S2500_IRQ_PSI            (1u << 1)
#define S2500_IRQ_LSI            (1u << 0)

/* DIOCFG0: 每路 DIO 8 bit: [7]SLW [6]PEN [5]PUD [4:1]FN [0]VAL */
#define S2500_DIO_SHIFT(n)       ((n) ? 8 : 0)
#define S2500_DIO_SLW(n)         (1u << (S2500_DIO_SHIFT(n) + 7))
#define S2500_DIO_PEN(n)         (1u << (S2500_DIO_SHIFT(n) + 6))
#define S2500_DIO_PUD(n)         (1u << (S2500_DIO_SHIFT(n) + 5))
#define S2500_DIO_FN(n, fn)      (((uint32_t)(fn) & 0xFu) << (S2500_DIO_SHIFT(n) + 1))
#define S2500_DIO_VAL(n, v)      ((uint32_t)((v) ? 1u : 0u) << S2500_DIO_SHIFT(n))

/* DIO 功能选择 (Table 11) */
#define S2500_DIOFN_DISABLE      0x0u
#define S2500_DIOFN_GPIO         0x1u
#define S2500_DIOFN_SFD_TX       0x2u
#define S2500_DIOFN_SFD_RX       0x3u
#define S2500_DIOFN_LED_LINKCTL  0x4u
#define S2500_DIOFN_LED_PLCASTS  0x5u
#define S2500_DIOFN_LED_TX       0x6u
#define S2500_DIOFN_LED_RX       0x7u
#define S2500_DIOFN_CLK25M       0x8u
#define S2500_DIOFN_CMP          0x9u
#define S2500_DIOFN_CAP          0xAu
#define S2500_DIOFN_SFD_RXTX     0xBu
#define S2500_DIOFN_BCN          0xCu
#define S2500_DIOFN_LED_TXBCN    0xDu
#define S2500_DIOFN_LED_TXRX     0xFu

/* TWEAKS2 */
#define S2500_TWEAKS2_TXAMP_SHIFT  14
#define S2500_TWEAKS2_EDTHCD_SHIFT 10
#define S2500_TWEAKS2_EDTH_SHIFT   6
#define S2500_TWEAKS2_TXSLW_SHIFT  4
#define S2500_TWEAKS2_TSD_DIS      (1u << 3)
#define S2500_TWEAKS2_TXCMC_SHIFT  1
#define S2500_TWEAKS2_CLKO_ENA     (1u << 0)

/* 25 MHz 时钟输出: CLK_O 使能位在 TWEAKS2[0], 默认开启 */

#endif /* S2500_REGS_H */
