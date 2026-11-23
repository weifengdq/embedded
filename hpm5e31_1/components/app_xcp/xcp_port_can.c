/*
 * xcp_port_can.c —— XCP on CAN 传输层（经典帧 500 kbit/s，单帧 CTO/DTO）
 *
 * 帧格式（ASAM XCP on CAN）：
 *   CMD 帧：CAN ID = cmd_id(0x555)，data[0] = 命令码，data[1..7] = 参数
 *   RES 帧：CAN ID = res_id(0x556)，data[0] = PID(0xFF/0xFE/0xFD)，data[1..7] = 数据
 *   DAQ 帧：CAN ID = res_id，data[0] = PID(0x00..0xFB = 绝对 ODT 号)，data[1..7] = ODT 数据
 *
 * 为什么 DAQ 和 RES 共用 0x556：XCPonCAN 允许 DTO 用同一个 CAN ID，靠 PID 区分；
 * 这样主机侧（pyxcp）只需要配 CAN_ID_MASTER/CAN_ID_SLAVE 两个 ID，A2L 之外不用再定义
 * 一堆 DAQ ID。另一种做法是每个 ODT 一个 CAN ID + PID_OFF，本组件不支持 PID_OFF。
 *
 * 经典帧 DLC 固定 8（不足补 0x00），这是 XCPonCAN 的常见约定：从站让主站按命令语义
 * 判断有效长度，避免主站对"短帧"做兼容处理。
 */
#include "xcp.h"
#include "xcp_port_can.h"

#include "hpm_mcan_drv.h"
#include "hpm_interrupt.h"
#include "board.h"
#include "ext/board_can.h"

#include <string.h>

/* HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM（hpm_soc_feature.h: MCAN_SOC_MSG_BUF_IN_AHB_RAM=1），
 * 必须在 mcan_get_default_config/mcan_init 之前用 mcan_set_msg_buf_attr() 注册缓冲区，
 * 否则 mcan_get_ram_size() 返回 0 -> mcan_init 返回 status_invalid_argument(2)。
 * 每个 MCAN 实例各需要一份 640 word = 2560 B。 */
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_can_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static MCAN_Type *s_base;
static uint16_t   s_cmd_id = 0x555u;
static uint16_t   s_res_id = 0x556u;
static bool       s_ext_id;
static bool       s_ready;
static uint32_t   s_busoff_cnt;
static uint32_t   s_rx_overflow;
static uint32_t   s_tx_full;

/* ------------------------------------------------------------------ */
/* 收发                                                                */
/* ------------------------------------------------------------------ */

static int can_send_dto(const uint8_t *buf, uint16_t len)
{
    mcan_tx_frame_t tx;
    uint32_t idx = 0u;

    if (!s_ready) {
        return -1;
    }
    if (len > 8u) {
        len = 8u;
    }
    memset(&tx, 0, sizeof(tx));
    tx.std_id = s_res_id;
    tx.use_ext_id = 0u;
    tx.rtr = 0u;
    tx.canfd_frame = 0u;
    tx.bitrate_switch = 0u;
    tx.dlc = 8u;                    /* 固定 8 字节，无效字节补 0 */
    if (len > 0u) {
        memcpy(tx.data_8, buf, len);
    }
    if (mcan_transmit_via_txfifo_nonblocking(s_base, &tx, &idx) != status_success) {
        s_tx_full++;
        return -1;
    }
    return 0;
}

static void can_poll(void)
{
    mcan_rx_message_t rx;
    uint8_t guard = 0u;

    if (!s_ready) {
        return;
    }
    /* 一次最多取 8 帧，避免长时间霸占主循环 */
    while (guard < 8u) {
        if (mcan_read_rxfifo(s_base, 0u, &rx) != status_success) {
            break;
        }
        guard++;
        if (rx.use_ext_id != 0u) {
            continue;
        }
        if (rx.std_id != s_cmd_id) {
            continue;               /* 同总线上别的节点/别的协议帧，忽略 */
        }
        uint16_t len = (rx.canfd_frame != 0u) ? 8u : (uint16_t) rx.dlc;
        if (len > 8u) {
            len = 8u;
        }
        if (len == 0u) {
            continue;
        }
        xcp_rx_cto(rx.data_8, len);
    }
    if (mcan_is_in_busoff_state(s_base)) {
        s_busoff_cnt++;
    }
}

/* ------------------------------------------------------------------ */
/* 初始化                                                              */
/* ------------------------------------------------------------------ */

int xcp_can_init(MCAN_Type *base, uint16_t cmd_id, uint16_t res_id, bool ext_id)
{
    mcan_config_t cfg;
    hpm_stat_t st;

    s_base = base;
    s_cmd_id = cmd_id;
    s_res_id = res_id;
    s_ext_id = ext_id;

    board_can_transceivers_enable();

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr = {(uint32_t) (uintptr_t) s_can_msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4u};
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    /* 经典帧 500 kbit/s，80 MHz CAN 时钟：1 + 139 + 20 = 160 TQ，采样点 87.5% */
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = 1u;
    cfg.can_timing.num_seg1 = 139u;
    cfg.can_timing.num_seg2 = 20u;
    cfg.can_timing.num_sjw = 4u;
    /* 不过滤：默认 GFC 接受所有非匹配帧，软件按 ID 过滤（见 can_poll） */
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    if (clk == 0u) {
        return -2;
    }
    mcan_deinit(base);
    st = mcan_init(base, &cfg, clk);
    if (st != status_success) {
        return (int) st;
    }
    s_ready = true;
    return 0;
}

void xcp_can_get_stats(uint32_t *busoff, uint32_t *rx_overflow, uint32_t *tx_full)
{
    if (busoff != NULL) {
        *busoff = s_busoff_cnt;
    }
    if (rx_overflow != NULL) {
        *rx_overflow = s_rx_overflow;
    }
    if (tx_full != NULL) {
        *tx_full = s_tx_full;
    }
}

void xcp_can_reset_stats(void)
{
    s_busoff_cnt = 0u;
    s_rx_overflow = 0u;
    s_tx_full = 0u;
}

const xcp_transport_ops_t xcp_can_ops = {
    .name = "CAN",
    .poll = can_poll,
    .send_dto = can_send_dto,
};
