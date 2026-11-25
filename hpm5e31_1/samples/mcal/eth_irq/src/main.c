/*
 * eth_irq —— AUTOSAR Eth 驱动接收中断模式实测（ISR 次数 / 中断→消费者延迟 / CPU 占用 / 丢包）
 *
 * 硬件：hpm5e31_1，ENET0(RGMII) + YT8011AN(1000BASE-T1) <-> 转换盒 <-> 主机 enp6s0
 * 驱动：mcal/Eth（Eth_Init 时把 RxInterruptEnable 置 TRUE 即进中断模式）
 * 说明：PBcfg 里默认 RxInterruptEnable=FALSE（轮询）；本样例在启动时用一份"运行期配置
 *       副本"重新 Eth_Init，把 RX 中断打开。这等价于改 Eth_PBcfg.c 后重编译，但不用
 *       为中断/轮询两种模式各维护一套 PBcfg —— 也顺便演示了 Eth_Init 可重复调用。
 *
 * 两种模式的取帧策略（同一份统计代码，口径可比）：
 *   轮询：主循环每一轮都调用 Eth_GetRxBuffer（Eth_Receive 之外唯一取帧入口），
 *         统计"空轮询间隔"作为轮询模式的新帧检测延迟上界。
 *   中断：只跟踪 Eth_GetRxIsrCount 的增量，ISR 到来后才排空 DMA 环（真正的 ISR 驱动），
 *         每帧用 Eth_GetLastIsrCycle 算 ISR→主循环消费延迟。
 *
 * 串口自证格式（一行一个窗口）：
 *   （旧格式，保留给老日志/脚本对照）
 *   IRQ mode=interrupt secs=4 frames=... bytes=... isr=... isr_per_frame=... lat_avg=...us lat_max=...us
 *   （新格式，一行给全）
 *   IRQX mode=interrupt win_us=... frames=... fps=... isr=... isr_pf_x1000=...
 *       isr_pf_x1000=... lat_mode=isr lat_avg_us=... lat_min_us=... lat_max_us=...
 *       isr_dmax=... polls=... poll_per_s=... poll_ns_avg=... poll_ns_max=...
 *       rx_cpu_x100=... idle_cpu_x100=... drop=... err=... crc=... ovf=...
 *   其中 lat_mode=na 表示轮询模式没有 ISR 时间戳，延迟用 poll_ns_*（两次取帧尝试的间隔）表达；
 *   *_x100 / *_x1000 是定点数（避免浮点 printf）：rx_cpu_x100=1234 表示 12.34%。
 *
 * 两个 R7 遗留 bug（本次修掉，draft §50.7.5 有说明）：
 *   1) 接收窗口单位写成 secs*cycles_per_us*1000，实际只有 ms —— 6 s 窗口只跑了 6 ms；
 *   2) 测试帧 magic 判在 data[0]/data[1]，而 Eth_GetRxBuffer 返回整帧（头占 14 B），
 *      广播帧首字节是 FF FF，永远匹配不上 —— ISR 有计数、frames 恒 0。
 *
 * 注：窗口用 bsp_uptime_ms() 计时（R7 版本错写成 secs*cycles_per_us*1000，实际只跑了几毫秒，
 *     本次修正为 secs*1000 ms；mcycle 只用于窗口内的延迟/占比差分，4 s 窗口 < 2^32 周期）。
 */
#include "bsp.h"
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include <stdio.h>
#include <string.h>

#define CTRL            0U
#define TRCV            0U
#define TEST_ETHERTYPE  0x88B5U
#define MAGIC0 'E'
#define MAGIC1 'T'
#define MAGIC2 'H'
#define MAGIC3 '0'

static Eth_CtrlConfigType s_cfg_copy;
static Eth_ConfigType     s_cfg_set;
static uint32 s_cycles_per_us = 480U;
static boolean s_irq_enabled;

/*! 单个统计窗口的累计量（全部整数，避免浮点） */
typedef struct {
    uint32 Frames;        /*!< 收到的测试帧数 */
    uint32 Bytes;         /*!< 测试帧净荷字节数 */
    uint32 RxCycles;      /*!< 主循环花在 Eth_GetRxBuffer 路径上的 mcycle */
    uint32 IdleCycles;    /*!< 其中"空轮询"（没有帧可取）的 mcycle */
    uint32 Polls;         /*!< Eth_GetRxBuffer 调用次数 */
    uint32 Loops;         /*!< 主循环轮数 */
    uint32 Isr;           /*!< 窗口内 RX 中断次数 */
    uint32 IsrDmax;       /*!< 相邻两次主循环检查之间 ISR 计数增量最大值（中断合并度） */
    uint32 LatSumUs;      /*!< ISR→消费延迟累加（µs，只算有效样本） */
    uint32 LatMaxUs;
    uint32 LatMinUs;
    uint32 LatSamples;    /*!< 有效延迟样本数 */
    uint32 LatBad;        /*!< 时间戳无效/回绕/延迟 >10 ms 的帧数（批量消费时"最近一次 ISR"口径的已知缺陷） */
    uint32 LatBucket[6];  /*!< <5 / 5-20 / 20-100 / 100-1000 / 1-10 ms / >10 ms */
    uint32 WakeSumUs;     /*!< ISR→主循环"发现"延迟累加（每次排空一个样本） */
    uint32 WakeMaxUs;
    uint32 WakeNum;
    uint32 PollGapSumNs;  /*!< 轮询模式空轮询间隔累加（ns） */
    uint32 PollGapMaxNs;
    uint32 PollGapCount;
    uint32 FramesIsr;     /*!< 由 ISR 驱动路径取到的帧数 */
    uint32 FramesFb;      /*!< 由"兜底轮询"取到的帧数（ISR 漏报时才 >0） */
    uint32 FbPolls;       /*!< 兜底轮询次数（中断模式每 1 ms 一次） */
    uint32 Drop, Err, Crc, Ovf;  /*!< 计数器增量 */
} RxWinType;

static uint32 now_cycle(void)
{
    return (uint32) hpm_csr_get_core_mcycle();
}

static uint32 cyc_to_ns(uint32 cyc)
{
    uint32 us = cyc / s_cycles_per_us;
    uint32 rem = cyc % s_cycles_per_us;
    return (us * 1000U) + ((rem * 1000U) / s_cycles_per_us);
}

/*! 用"运行期配置副本"重新初始化：interrupt=TRUE 开 RX 中断，FALSE 回轮询
 *
 * 易错点（实测：从"中断模式"直接调 Eth_Init 切回轮询会整机卡死）：
 *   Eth_Init 第一步就 memset 掉驱动状态（Base=NULL），而 PLIC 里 ENET0 中断此时仍是开的；
 *   紧接着 board_reset_enet_phy() 要拉低 PHY 复位 30 ms，这 30 ms 里只要来一帧（链路上的
 *   广播/协议帧，启动后实测 isr 已经非 0）就会进 ISR —— ISR 看到 Base==NULL 直接返回，
 *   不写 DMA_STATUS W1C → 中断线一直有效 → ISR 风暴，主循环饿死，串口再无输出。
 *   修法：切换前先把控制器停到 DOWN、关掉 PLIC，再走 Eth_Init（Eth_Init 自己会清挂起位）。
 *   驱动层面更彻底的做法是在 Eth_Init 入口做同样的"旧状态收尾"，见 draft 的"建议"。 */
static void reinit(boolean interrupt)
{
    if (s_irq_enabled) {
        (void) Eth_SetControllerMode(CTRL, ETH_MODE_DOWN);   /* DMA ST/SR=0，不再产生新 RI */
        Eth_BoardIrqEnable(CTRL, (void *) HPM_ENET0, FALSE); /* 关 PLIC，堵住 ISR 窗口 */
    }
    printf("REINIT begin old_irq=%u target=%u\r\n", (unsigned) s_irq_enabled,
           (unsigned) interrupt);

    s_cfg_copy = Eth_Config.CtrlConfigPtr[0];
    s_cfg_copy.RxInterruptEnable = interrupt;
    s_cfg_copy.TxInterruptEnable = FALSE;
    s_cfg_set.CtrlConfigPtr = &s_cfg_copy;
    s_cfg_set.CtrlCount = 1U;

    Eth_Init(&s_cfg_set);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
    s_irq_enabled = interrupt;
    printf("IRQ mode=%s det=%u\r\n", interrupt ? "interrupt" : "polling",
           (unsigned) Det_GetErrorCount());
}

/*! 收一帧测试帧（含 magic 校验），返回时已归还缓冲 */
static boolean recv_test(uint16 *len_out)
{
    const uint8 *data = NULL;
    uint16 l = 0U;
    Eth_FrameType type = 0U;
    Eth_BufIdxType h = 0U;
    boolean got = FALSE;

    while (Eth_GetRxBuffer(CTRL, 0U, &h, &data, &l, &type) != ETH_NOT_RECEIVED) {
        /* 注意：Eth_GetRxBuffer 返回的是"整帧"指针（以太网头在 data[0..13]），
         * magic 在净荷里、偏移 ETH_HEADER_LEN=14 —— R7 版本错在 data[0]/data[1] 判断，
         * 广播帧目的 MAC 是 FF FF，永远匹配不上，所以"ISR 有数、frames 恒 0"。 */
        if ((type == (Eth_FrameType) TEST_ETHERTYPE) && (l >= (uint16) (ETH_HEADER_LEN + 8U)) &&
            (data[ETH_HEADER_LEN + 0U] == (uint8) MAGIC0) &&
            (data[ETH_HEADER_LEN + 1U] == (uint8) MAGIC1)) {
            *len_out = l;
            got = TRUE;
            break;              /* 保留 h，稍后归还 */
        }
        Eth_ReleaseRxBuffer(CTRL, h);
    }
    if (got) {
        Eth_ReleaseRxBuffer(CTRL, h);
    }
    return got;
}

/*! 记一帧；have_isr=TRUE 时同时累计 ISR→消费延迟
 *
 * 口径说明（写进 draft 的"易错点"）：Eth_GetLastIsrCycle 是"最近一次 RX 中断进入"的时刻，
 * 一次中断覆盖多帧时，后几帧的这个时间戳可能比"当前时刻"还晚（批量排空时 ISR 仍在进来），
 * 直接相减会得到负数 → 强转 unsigned 变成 ~8.9 s 的假最大值。所以这里只统计
 *   isr_cyc 有效、时间差为正、且 < 10 ms 的样本，其余计入 lat_bad；
 * 同时给出延迟分桶分布，避免被个别离群值带偏。 */
static void count_frame(RxWinType *w, uint16 flen, boolean have_isr)
{
    uint32 cyc, lat_us;

    w->Frames++;
    w->Bytes += flen;
    if (!have_isr) {
        return;
    }
    cyc = (uint32) (now_cycle() - Eth_GetLastIsrCycle(CTRL));
    if ((Eth_GetLastIsrCycle(CTRL) == 0U) || (((sint32) cyc) < 0)) {
        w->LatBad++;
        return;
    }
    lat_us = cyc / s_cycles_per_us;
    if (lat_us > 10000U) {
        w->LatBad++;
        return;
    }
    w->LatSamples++;
    w->LatSumUs += lat_us;
    if (lat_us > w->LatMaxUs) {
        w->LatMaxUs = lat_us;
    }
    if (lat_us < w->LatMinUs) {
        w->LatMinUs = lat_us;
    }
    if (lat_us < 5U) {
        w->LatBucket[0]++;
    } else if (lat_us < 20U) {
        w->LatBucket[1]++;
    } else if (lat_us < 100U) {
        w->LatBucket[2]++;
    } else if (lat_us < 1000U) {
        w->LatBucket[3]++;
    } else if (lat_us <= 10000U) {
        w->LatBucket[4]++;
    } else {
        w->LatBucket[5]++;
    }
}

static void cmd_info(void)
{
    Eth_CounterType c;
    Eth_RxStatusType st = ETH_NOT_RECEIVED;

    (void) Eth_GetCounterValues(CTRL, &c);
    Eth_Receive(CTRL, 0U, &st);
    printf("IRQ mode=%s isr=%u det=%u rx_status=%u drop=%u err=%u crc=%u ovf=%u\r\n",
           s_irq_enabled ? "interrupt" : "polling",
           (unsigned) Eth_GetRxIsrCount(CTRL), (unsigned) Det_GetErrorCount(),
           (unsigned) st, (unsigned) c.DiscInbdPkt, (unsigned) c.ErrInbdPkt,
           (unsigned) c.DropPktCrc, (unsigned) c.DropPktBufOverrun);
}

/*! 接收窗口：帧数/吞吐/ISR 次数/ISR→消费延迟/轮询间隔/CPU 占比/丢帧 */
static void cmd_rx(uint32 secs)
{
    RxWinType w;
    Eth_CounterType c0, c1;
    uint16 flen = 0U;
    uint32 t0_ms, t_cyc0, win_cyc, isr0, last_isr;
    uint32 prev_empty_start = 0U, lat_den;
    uint32 last_fb_ms;
    boolean prev_empty = FALSE;
    uint32 i;

    if ((secs == 0U) || (secs > 8U)) {
        printf("[rx] bad secs %u (1..8)\r\n", (unsigned) secs);
        return;
    }
    (void) memset(&w, 0, sizeof(w));
    w.LatMinUs = 0xFFFFFFFFU;

    /* 有界排空窗口前的积压：否则切换/命令解析期间到达的老帧会污染延迟统计；
     * 高速灌包时环里一直有帧，不能无限排，最多清 256 帧 */
    for (i = 0U; (i < 256U) && recv_test(&flen); i++) {
    }

    Eth_GetCounterValues(CTRL, &c0);
    isr0 = Eth_GetRxIsrCount(CTRL);
    last_isr = isr0;
    t0_ms = bsp_uptime_ms();
    last_fb_ms = t0_ms;
    t_cyc0 = now_cycle();

    while ((uint32) (bsp_uptime_ms() - t0_ms) < (secs * 1000U)) {
        if (!s_irq_enabled) {
            uint32 p0 = now_cycle();
            boolean got = recv_test(&flen);
            uint32 p1 = now_cycle();

            w.RxCycles += (p1 - p0);
            w.Polls++;
            if (!got) {
                w.IdleCycles += (p1 - p0);
                if (prev_empty) {
                    uint32 dt_ns = cyc_to_ns(p0 - prev_empty_start);
                    w.PollGapSumNs += dt_ns;
                    if (dt_ns > w.PollGapMaxNs) {
                        w.PollGapMaxNs = dt_ns;
                    }
                    w.PollGapCount++;
                }
                prev_empty_start = p0;
                prev_empty = TRUE;
            } else {
                prev_empty = FALSE;
                count_frame(&w, flen, FALSE);
            }
        } else {
            uint32 isr = Eth_GetRxIsrCount(CTRL);
            if (isr != last_isr) {
                uint32 d = isr - last_isr;
                if (d > w.IsrDmax) {
                    w.IsrDmax = d;
                }
                last_isr = isr;
                /* "ISR→主循环发现"延迟：中断入口到主循环看到计数变化，衡量唤醒开销 */
                {
                    uint32 lc = Eth_GetLastIsrCycle(CTRL);
                    uint32 wcyc = (uint32) (now_cycle() - lc);
                    if ((lc != 0U) && (((sint32) wcyc) >= 0) && ((wcyc / s_cycles_per_us) <= 10000U)) {
                        uint32 wus = wcyc / s_cycles_per_us;
                        w.WakeSumUs += wus;
                        if (wus > w.WakeMaxUs) {
                            w.WakeMaxUs = wus;
                        }
                        w.WakeNum++;
                    }
                }
                /* 一次 ISR 可能覆盖多帧：把环里已有的帧全部消费掉 */
                while ((uint32) (bsp_uptime_ms() - t0_ms) < (secs * 1000U)) {
                    uint32 q0 = now_cycle();
                    boolean got = recv_test(&flen);
                    uint32 q1 = now_cycle();

                    w.RxCycles += (q1 - q0);
                    w.Polls++;
                    if (!got) {
                        w.IdleCycles += (q1 - q0);
                        break;
                    }
                    w.FramesIsr++;
                    count_frame(&w, flen, TRUE);
                }
            }
            /* 兜底轮询：每 1 ms 查一次 DMA 环，验证"ISR 有没有漏帧"。
             * 纯 ISR 门控时这里应恒为 0 帧；若 >0 说明有帧到了却没进中断。 */
            if ((uint32) (bsp_uptime_ms() - last_fb_ms) >= 1U) {
                uint32 q0, q1;
                boolean got;

                last_fb_ms = bsp_uptime_ms();
                w.FbPolls++;
                q0 = now_cycle();
                got = recv_test(&flen);
                q1 = now_cycle();
                w.RxCycles += (q1 - q0);
                if (got) {
                    w.FramesFb++;
                    count_frame(&w, flen, FALSE);   /* 兜底路径没有可靠的 ISR 时间戳，不计延迟 */
                }
            }
        }
        Eth_MainFunction();
        w.Loops++;
    }
    win_cyc = (uint32) (now_cycle() - t_cyc0);
    Eth_GetCounterValues(CTRL, &c1);

    w.Isr = Eth_GetRxIsrCount(CTRL) - isr0;
    w.Drop = c1.DiscInbdPkt - c0.DiscInbdPkt;
    w.Err = c1.ErrInbdPkt - c0.ErrInbdPkt;
    w.Crc = c1.DropPktCrc - c0.DropPktCrc;
    w.Ovf = c1.DropPktBufOverrun - c0.DropPktBufOverrun;
    lat_den = (w.LatSamples != 0U) ? w.LatSamples : 1U;
    if (w.LatMinUs == 0xFFFFFFFFU) {
        w.LatMinUs = 0U;
    }
    printf("IRQ mode=%s secs=%u frames=%u bytes=%u isr=%u isr_per_frame=%u lat_avg=%uus lat_max=%uus\r\n",
           s_irq_enabled ? "interrupt" : "polling", (unsigned) secs,
           (unsigned) w.Frames, (unsigned) w.Bytes, (unsigned) w.Isr,
           (unsigned) ((w.Frames != 0U) ? ((w.Isr * 100U) / w.Frames) : 0U),
           (unsigned) (w.LatSumUs / lat_den), (unsigned) w.LatMaxUs);
    printf("IRQX mode=%s win_us=%u fps=%u isr_pf_x1000=%u lat_mode=%s lat_avg_us=%u lat_min_us=%u "
           "lat_max_us=%u lat_n=%u lat_bad=%u lb_lt5=%u lb_5_20=%u lb_20_100=%u lb_100_1m=%u "
           "lb_1_10m=%u wake_avg_us=%u wake_max_us=%u wake_n=%u isr_dmax=%u frames_isr=%u "
           "frames_fb=%u fb_polls=%u polls=%u poll_per_s=%u poll_ns_avg=%u poll_ns_max=%u "
           "rx_cpu_x100=%u idle_cpu_x100=%u loops=%u drop=%u err=%u crc=%u ovf=%u\r\n",
           s_irq_enabled ? "interrupt" : "polling",
           (unsigned) (win_cyc / s_cycles_per_us),
           (unsigned) ((uint32) (((uint64) w.Frames * 1000000U) / (win_cyc / s_cycles_per_us))),
           (unsigned) ((w.Frames != 0U) ? ((uint32) (((uint64) w.Isr * 1000U) / w.Frames)) : 0U),
           s_irq_enabled ? "isr" : "na",
           (unsigned) (w.LatSumUs / lat_den), (unsigned) w.LatMinUs, (unsigned) w.LatMaxUs,
           (unsigned) w.LatSamples, (unsigned) w.LatBad,
           (unsigned) w.LatBucket[0], (unsigned) w.LatBucket[1], (unsigned) w.LatBucket[2],
           (unsigned) w.LatBucket[3], (unsigned) w.LatBucket[4],
           (unsigned) ((w.WakeNum != 0U) ? (w.WakeSumUs / w.WakeNum) : 0U),
           (unsigned) w.WakeMaxUs, (unsigned) w.WakeNum,
           (unsigned) w.IsrDmax, (unsigned) w.FramesIsr, (unsigned) w.FramesFb, (unsigned) w.FbPolls,
           (unsigned) w.Polls,
           (unsigned) ((uint32) (((uint64) w.Polls * 1000000U) / (win_cyc / s_cycles_per_us))),
           (unsigned) ((w.PollGapCount != 0U) ? (w.PollGapSumNs / w.PollGapCount) : 0U),
           (unsigned) w.PollGapMaxNs,
           (unsigned) ((uint32) (((uint64) w.RxCycles * 10000U) / win_cyc)),
           (unsigned) ((uint32) (((uint64) w.IdleCycles * 10000U) / win_cyc)),
           (unsigned) w.Loops, (unsigned) w.Drop, (unsigned) w.Err,
           (unsigned) w.Crc, (unsigned) w.Ovf);
}

/*! 发 n 帧到广播（主机 listen 模式收，用来验证 ISR 计数来源） */
static void cmd_burst(uint32 len, uint32 n)
{
    uint32 i, ok = 0U;
    Eth_BufIdxType idx;
    uint8 *buf = NULL;
    uint16 grant;

    for (i = 0U; i < n; i++) {
        grant = (uint16) len;
        if (Eth_ProvideTxBuffer(CTRL, 0U, &idx, &buf, &grant) == BUFREQ_OK) {
            uint8 *pl = &buf[ETH_HEADER_LEN];
            uint8 dst[6] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
            pl[0] = (uint8) MAGIC0; pl[1] = (uint8) MAGIC1;
            pl[2] = (uint8) MAGIC2; pl[3] = (uint8) MAGIC3;
            pl[4] = (uint8) (i & 0xFFU); pl[5] = (uint8) ((i >> 8) & 0xFFU);
            pl[6] = (uint8) ((i >> 16) & 0xFFU); pl[7] = (uint8) ((i >> 24) & 0xFFU);
            if (Eth_Transmit(CTRL, idx, (Eth_FrameType) TEST_ETHERTYPE, TRUE, (uint16) len, dst) == E_OK) {
                ok++;
            }
        }
        Eth_TxConfirmation(CTRL);
    }
    Eth_TxConfirmation(CTRL);
    printf("BURST len=%u n=%u ok=%u\r\n", (unsigned) len, (unsigned) n, (unsigned) ok);
}

static void cmd_irq(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "info";
    uint32 a = 0U, b = 0U;

    if (strcmp(sub, "info") == 0) {
        cmd_info();
    } else if (strcmp(sub, "on") == 0) {
        reinit(TRUE);
    } else if (strcmp(sub, "off") == 0) {
        reinit(FALSE);
    } else if (strcmp(sub, "rx") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 5U;
        cmd_rx(a);
    } else if (strcmp(sub, "burst") == 0) {
        a = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 512U;
        b = (argc >= 4) ? (uint32) strtoul(argv[3], NULL, 0) : 1000U;
        cmd_burst(a, b);
    } else {
        printf("unknown sub: %s (info|on|off|rx [s]|burst [len] [n])\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irq, cmd_irq, ETH RX interrupt test: irq info|on|off|rx [s]|burst [len] [n]);

int main(void)
{
    uint32 cpu_mhz;

    bsp_init();
    cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    s_cycles_per_us = (cpu_mhz != 0U) ? cpu_mhz : 480U;

    printf("\r\n=== eth_irq: AUTOSAR Eth RX-interrupt mode, ENET0 + YT8011AN ===\r\n");
    Det_Init();
    Det_ClearErrorCount();

    Eth_Init(&Eth_Config);
    EthTrcv_Init(&EthTrcv_Config);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);

    /* 默认进中断模式；'irq off' 可切回轮询做对照 */
    reinit(TRUE);
    printf("[boot] cpu=%u MHz, tx=%u x %uB, rx=%u x %uB\r\n", (unsigned) cpu_mhz,
           (unsigned) ETH_TX_BUF_COUNT, (unsigned) ETH_TX_BUF_SIZE,
           (unsigned) ETH_RX_BUF_COUNT, (unsigned) ETH_RX_BUF_SIZE);
    cmd_info();

    while (1) {
        Eth_MainFunction();
        EthTrcv_MainFunction();
        bsp_shell_poll();
    }
}