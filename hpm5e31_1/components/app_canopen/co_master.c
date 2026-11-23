/**
 * Minimal CANopen master / test client - implementation (see co_master.h).
 *
 * @file        co_master.c
 * @copyright   Apache-2.0 / hpm5e31_1 项目组
 */
#include <string.h>

#include "co_master.h"

#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include "board.h"
#include "ext/board_can.h"

#define CO_M_RSDO 0x580U
#define CO_M_TSDO 0x600U
#define CO_M_NMT  0x000U
#define CO_M_SYNC 0x080U

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_master_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static const mcan_filter_elem_t s_master_accept_all_std = {
    MCAN_FILTER_TYPE_CLASSIC_FILTER,
    MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH,
    0U,
    0U,
    .filter_id = 0U,
    .filter_mask = 0U,
};

static uint32_t m_now_us(co_master_t* m) {
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) m->cpu_mhz);
}

static void m_service(co_master_t* m) {
    if (m->service != NULL) {
        m->service();
    }
}

static int m_hw_init(MCAN_Type* base, uint16_t kbps) {
    mcan_config_t cfg;
    uint32_t clk;
    uint32_t presc;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) s_master_msg_buf, (uint32_t) sizeof(s_master_msg_buf)};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    clk = board_init_can_clock(base);
    presc = (kbps == 0U) ? 10U : (clk / ((uint32_t) kbps * 1000U * 16U));
    if (presc == 0U) {
        presc = 1U;
    }
    cfg.can_timing.prescaler = (uint16_t) presc;
    cfg.can_timing.num_seg1 = 13U;
    cfg.can_timing.num_seg2 = 2U;
    cfg.can_timing.num_sjw = 2U;
    cfg.all_filters_config.std_id_filter_list.filter_elem_list = &s_master_accept_all_std;
    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 1U;
    cfg.all_filters_config.ext_id_filter_list.filter_elem_list = NULL;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0U;
    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = true;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = true;
    board_init_can(base);
    mcan_deinit(base);
    return (mcan_init(base, &cfg, clk) == status_success) ? 0 : -1;
}

int co_master_init(co_master_t* m, MCAN_Type* base, uint16_t bitrate_kbps) {
    if ((m == NULL) || (base == NULL)) {
        return -1;
    }
    (void) memset(m, 0, sizeof(*m));
    m->can = base;
    m->cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    if (m->cpu_mhz == 0U) {
        m->cpu_mhz = 480U;
    }
    board_can_transceivers_enable();
    return m_hw_init(base, bitrate_kbps);
}

void co_master_set_node(co_master_t* m, uint8_t node) {
    if (m != NULL) {
        m->target_node = node;
    }
}

void co_master_set_service(co_master_t* m, void (*service)(void)) {
    if (m != NULL) {
        m->service = service;
    }
}

void co_master_disable(co_master_t* m) {
    if ((m != NULL) && (m->can != NULL)) {
        mcan_deinit(m->can);
        m->can = NULL;
    }
}

uint32_t co_master_poll(co_master_t* m) {
    mcan_rx_message_t rx;
    uint32_t got = 0U;
    uint32_t head;
    uint32_t next;
    uint32_t i;

    if ((m == NULL) || (m->can == NULL)) {
        return 0U;
    }
    while (mcan_read_rxfifo(m->can, 0U, &rx) == status_success) {
        co_master_frame_t* slot;
        if ((rx.use_ext_id != 0U) || (rx.canfd_frame != 0U)) {
            continue;
        }
        head = m->fifo_head;
        next = (head + 1U) % CO_MASTER_FIFO_LEN;
        if (next == m->fifo_tail) {
            m->fifo_lost++; /* 队列满：丢弃最旧？
                               这里丢最新帧，避免改 tail（上位统计用 lost 指示） */
            continue;
        }
        slot = &m->fifo[head];
        slot->id = (uint16_t) (rx.std_id & 0x7FFU);
        slot->dlc = (uint8_t) ((rx.dlc > 8U) ? 8U : rx.dlc);
        for (i = 0U; i < 8U; i++) {
            slot->data[i] = rx.data_8[i];
        }
        slot->ts_us = m_now_us(m);
        if (slot->id == (uint16_t) (0x700U + m->target_node)) {
            m->hb_frames++;
        } else if (slot->id == (uint16_t) (0x080U + m->target_node)) {
            m->emcy_frames++;
            (void) memcpy(m->emcy_last, slot->data, 8);
        }
        m->fifo_head = next;
        m->rx_frames++;
        got++;
    }
    return got;
}

int co_master_pop(co_master_t* m, co_master_frame_t* frame) {
    if ((m == NULL) || (frame == NULL) || (m->fifo_tail == m->fifo_head)) {
        return 0;
    }
    *frame = m->fifo[m->fifo_tail];
    m->fifo_tail = (m->fifo_tail + 1U) % CO_MASTER_FIFO_LEN;
    return 1;
}

static int m_tx(co_master_t* m, uint16_t id, const uint8_t* data, uint8_t dlc) {
    mcan_tx_frame_t tx;
    uint32_t deadline;
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        tx.data_8[i] = (i < dlc) ? data[i] : 0U;
    }
    tx.std_id = (uint16_t) (id & 0x7FFU);
    tx.dlc = dlc;
    tx.use_ext_id = 0U;
    tx.rtr = 0U;
    tx.canfd_frame = 0U;
    tx.bitrate_switch = 0U;

    if (mcan_transmit_via_txfifo_nonblocking(m->can, &tx, NULL) == status_success) {
        m->tx_frames++;
        return 0;
    }
    /* TX FIFO 满：短等待后重试，期间继续服务从站 */
    deadline = m_now_us(m) + 2000U;
    while ((int32_t) (m_now_us(m) - deadline) < 0) {
        co_master_poll(m);
        m_service(m);
        if (mcan_transmit_via_txfifo_nonblocking(m->can, &tx, NULL) == status_success) {
            m->tx_frames++;
            return 0;
        }
    }
    return -1;
}

int co_master_send(co_master_t* m, uint16_t id, const uint8_t* data, uint8_t dlc) {
    if ((m == NULL) || (dlc > 8U) || ((data == NULL) && (dlc > 0U))) {
        return -1;
    }
    return m_tx(m, id, data, dlc);
}

int co_master_nmt(co_master_t* m, uint8_t node, uint8_t cmd) {
    uint8_t d[2];
    d[0] = cmd;
    d[1] = node;
    return m_tx(m, CO_M_NMT, d, 2U);
}

int co_master_sync(co_master_t* m) {
    return m_tx(m, CO_M_SYNC, NULL, 0U);
}

int co_master_sync_counter(co_master_t* m, uint8_t counter) {
    return m_tx(m, CO_M_SYNC, &counter, 1U);
}

/** 等待指定 ID 的帧；队列里的其他帧被丢弃（监视用 mon 已单独记录）。 */
static int m_wait_id(co_master_t* m, uint16_t id, uint32_t timeout_ms, co_master_frame_t* frame) {
    uint32_t deadline = m_now_us(m) + timeout_ms * 1000U;
    co_master_frame_t f;

    while (1) {
        co_master_poll(m);
        while (co_master_pop(m, &f)) {
            if (f.id == id) {
                if (frame != NULL) {
                    *frame = f;
                }
                return 0;
            }
        }
        m_service(m);
        if ((int32_t) (m_now_us(m) - deadline) >= 0) {
            return -1;
        }
    }
}

int co_master_wait_id(co_master_t* m, uint16_t id, uint32_t timeout_ms, co_master_frame_t* frame) {
    return m_wait_id(m, id, timeout_ms, frame);
}

void co_master_delay_ms(co_master_t* m, uint32_t ms) {
    uint32_t deadline = m_now_us(m) + ms * 1000U;
    while ((int32_t) (m_now_us(m) - deadline) < 0) {
        co_master_poll(m);
        m_service(m);
    }
}

uint32_t co_master_now_us(co_master_t* m) {
    return m_now_us(m);
}

/* ------------------------------ SDO client ------------------------------ */
#define SDO_ABORT(b0, f) (((uint32_t) (f).data[4]) | ((uint32_t) (f).data[5] << 8) | \
                          ((uint32_t) (f).data[6] << 16) | ((uint32_t) (f).data[7] << 24))

static int sdo_abort(co_master_t* m, const co_master_frame_t* f) {
    m->sdo_aborts++;
    m->last_abort_code = SDO_ABORT(0, *f);
    return -2;
}

static int sdo_upload_segments(co_master_t* m, uint8_t* buf, uint32_t cap, uint32_t* len, uint32_t timeout_ms) {
    uint8_t req[8];
    uint8_t toggle = 0U;
    uint32_t total = 0U;

    while (1) {
        co_master_frame_t f;
        uint8_t b0;
        uint8_t n;
        uint8_t cnt;

        (void) memset(req, 0, sizeof(req));
        req[0] = (uint8_t) (0x60U | (uint8_t) (toggle << 4));
        if (m_tx(m, (uint16_t) (CO_M_TSDO + m->target_node), req, 8U) != 0) {
            return -3;
        }
        if (m_wait_id(m, (uint16_t) (CO_M_RSDO + m->target_node), timeout_ms, &f) != 0) {
            m->sdo_timeouts++;
            return -1;
        }
        if (f.dlc < 8U) {
            return -3;
        }
        b0 = f.data[0];
        if ((b0 & 0x80U) != 0U) {
            return sdo_abort(m, &f);
        }
        if ((b0 & 0xE0U) != 0x00U) {
            return -3;
        }
        if (((b0 >> 4) & 0x01U) != toggle) {
            return -3;
        }
        n = (uint8_t) ((b0 >> 1) & 0x07U);
        cnt = (uint8_t) (7U - n);
        if ((total + cnt) > cap) {
            return -4;
        }
        (void) memcpy(&buf[total], &f.data[1], cnt);
        total += cnt;
        if ((b0 & 0x01U) != 0U) {
            break;
        }
        toggle ^= 1U;
    }
    if (len != NULL) {
        *len = total;
    }
    return 0;
}

int co_master_sdo_read(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint32_t* value,
                       uint32_t timeout_ms) {
    uint8_t req[8];
    co_master_frame_t f;
    uint8_t b0;
    uint8_t n;
    uint8_t size;
    uint32_t tmp;

    if ((m == NULL) || (value == NULL)) {
        return -1;
    }
    m->target_node = node;
    (void) memset(req, 0, sizeof(req));
    req[0] = 0x40U; /* 0x40: ccs=2 (initiate upload), expedited, no size */
    req[1] = (uint8_t) (index & 0xFFU);
    req[2] = (uint8_t) (index >> 8);
    req[3] = sub;
    if (m_tx(m, (uint16_t) (CO_M_TSDO + node), req, 8U) != 0) {
        return -3;
    }
    if (m_wait_id(m, (uint16_t) (CO_M_RSDO + node), timeout_ms, &f) != 0) {
        m->sdo_timeouts++;
        return -1;
    }
    if (f.dlc < 8U) {
        return -3;
    }
    b0 = f.data[0];
    if ((b0 & 0x80U) != 0U) {
        return sdo_abort(m, &f);
    }
    if (b0 == 0x41U) {
        /* segmented upload initiate: 用通用 upload 路径继续 */
        uint8_t buf[8];
        uint32_t len = 0U;
        int r = sdo_upload_segments(m, buf, sizeof(buf), &len, timeout_ms);
        if (r != 0) {
            return r;
        }
        if (len == 0U) {
            return -3;
        }
        tmp = 0U;
        for (uint8_t i = 0U; (i < len) && (i < 4U); i++) {
            tmp |= (uint32_t) buf[i] << (8U * i);
        }
        *value = tmp;
        return 0;
    }
    if ((b0 & 0xE0U) == 0x40U) {
        /* expedited upload response */
        n = (uint8_t) ((b0 >> 2) & 0x03U);
        size = (uint8_t) (4U - n);
        tmp = (uint32_t) f.data[4] | ((uint32_t) f.data[5] << 8) | ((uint32_t) f.data[6] << 16)
              | ((uint32_t) f.data[7] << 24);
        if (size < 4U) {
            tmp &= (1UL << (size * 8U)) - 1UL;
        }
        *value = tmp;
        return 0;
    }
    return -3;
}

int co_master_sdo_write(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint32_t value, uint8_t size,
                        uint32_t timeout_ms) {
    uint8_t req[8];
    co_master_frame_t f;
    uint8_t b0;

    if ((m == NULL) || (size == 0U) || (size > 4U)) {
        return -1;
    }
    m->target_node = node;
    (void) memset(req, 0, sizeof(req));
    req[0] = (uint8_t) (0x23U | (uint8_t) ((4U - size) << 2));
    req[1] = (uint8_t) (index & 0xFFU);
    req[2] = (uint8_t) (index >> 8);
    req[3] = sub;
    req[4] = (uint8_t) (value & 0xFFU);
    req[5] = (uint8_t) ((value >> 8) & 0xFFU);
    req[6] = (uint8_t) ((value >> 16) & 0xFFU);
    req[7] = (uint8_t) ((value >> 24) & 0xFFU);
    if (m_tx(m, (uint16_t) (CO_M_TSDO + node), req, 8U) != 0) {
        return -3;
    }
    if (m_wait_id(m, (uint16_t) (CO_M_RSDO + node), timeout_ms, &f) != 0) {
        m->sdo_timeouts++;
        return -1;
    }
    if (f.dlc < 8U) {
        return -3;
    }
    b0 = f.data[0];
    if ((b0 & 0x80U) != 0U) {
        return sdo_abort(m, &f);
    }
    if ((b0 & 0xE0U) != 0x60U) {
        return -3;
    }
    return 0;
}

int co_master_sdo_upload(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint8_t* buf, uint32_t cap,
                         uint32_t* len, uint32_t timeout_ms) {
    uint8_t req[8];
    co_master_frame_t f;
    uint8_t b0;
    uint8_t n;
    uint8_t size;

    if ((m == NULL) || (buf == NULL) || (len == NULL) || (cap == 0U)) {
        return -1;
    }
    m->target_node = node;
    (void) memset(req, 0, sizeof(req));
    req[0] = 0x40U;
    req[1] = (uint8_t) (index & 0xFFU);
    req[2] = (uint8_t) (index >> 8);
    req[3] = sub;
    if (m_tx(m, (uint16_t) (CO_M_TSDO + node), req, 8U) != 0) {
        return -3;
    }
    if (m_wait_id(m, (uint16_t) (CO_M_RSDO + node), timeout_ms, &f) != 0) {
        m->sdo_timeouts++;
        return -1;
    }
    if (f.dlc < 8U) {
        return -3;
    }
    b0 = f.data[0];
    if ((b0 & 0x80U) != 0U) {
        return sdo_abort(m, &f);
    }
    if (b0 == 0x41U) {
        return sdo_upload_segments(m, buf, cap, len, timeout_ms);
    }
    if ((b0 & 0xE0U) == 0x40U) {
        /* expedited: 直接返回 1..4 字节 */
        n = (uint8_t) ((b0 >> 2) & 0x03U);
        size = (uint8_t) (4U - n);
        if (size > cap) {
            return -4;
        }
        (void) memcpy(buf, &f.data[4], size);
        *len = size;
        return 0;
    }
    return -3;
}

int co_master_sdo_download(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, const uint8_t* buf,
                           uint32_t len, uint32_t timeout_ms) {
    uint8_t req[8];
    co_master_frame_t f;
    uint32_t off;
    uint8_t toggle;

    if ((m == NULL) || (buf == NULL) || (len == 0U)) {
        return -1;
    }
    m->target_node = node;
    (void) memset(req, 0, sizeof(req));
    req[0] = 0x21U; /* ccs=1, e=0, s=1: 分段下载 + 指明长度 */
    req[1] = (uint8_t) (index & 0xFFU);
    req[2] = (uint8_t) (index >> 8);
    req[3] = sub;
    req[4] = (uint8_t) (len & 0xFFU);
    req[5] = (uint8_t) ((len >> 8) & 0xFFU);
    req[6] = (uint8_t) ((len >> 16) & 0xFFU);
    req[7] = (uint8_t) ((len >> 24) & 0xFFU);
    if (m_tx(m, (uint16_t) (CO_M_TSDO + node), req, 8U) != 0) {
        return -3;
    }
    if (m_wait_id(m, (uint16_t) (CO_M_RSDO + node), timeout_ms, &f) != 0) {
        m->sdo_timeouts++;
        return -1;
    }
    if (f.dlc < 8U) {
        return -3;
    }
    if ((f.data[0] & 0x80U) != 0U) {
        return sdo_abort(m, &f);
    }
    if ((f.data[0] & 0xE0U) != 0x60U) {
        return -3;
    }
    off = 0U;
    toggle = 0U;
    while (off < len) {
        uint32_t chunk = len - off;
        uint8_t n;
        uint8_t i;
        if (chunk > 7U) {
            chunk = 7U;
        }
        n = (uint8_t) (7U - chunk);
        (void) memset(req, 0, sizeof(req));
        req[0] = (uint8_t) ((uint8_t) (n << 1) | (uint8_t) (toggle << 4)
                            | (uint8_t) (((off + chunk) >= len) ? 0x01U : 0x00U));
        for (i = 0U; i < chunk; i++) {
            req[1U + i] = buf[off + i];
        }
        if (m_tx(m, (uint16_t) (CO_M_TSDO + node), req, 8U) != 0) {
            return -3;
        }
        if (m_wait_id(m, (uint16_t) (CO_M_RSDO + node), timeout_ms, &f) != 0) {
            m->sdo_timeouts++;
            return -1;
        }
        if (f.dlc < 8U) {
            return -3;
        }
        if ((f.data[0] & 0x80U) != 0U) {
            return sdo_abort(m, &f);
        }
        if ((f.data[0] & 0xE0U) != 0x20U) {
            return -3;
        }
        if (((f.data[0] >> 4) & 0x01U) != toggle) {
            return -3;
        }
        off += chunk;
        toggle ^= 1U;
    }
    return 0;
}

void co_master_mon_print(co_master_t* m, uint32_t n) {
    uint32_t i;
    uint32_t tail;
    uint32_t count;

    if (m == NULL) {
        return;
    }
    tail = m->fifo_tail;
    count = m->fifo_head;
    if (count < tail) {
        count += CO_MASTER_FIFO_LEN;
    }
    count -= tail;
    if ((n != 0U) && (n < count)) {
        count = n;
    }
    printf("mon: %u frames in queue, rx=%u tx=%u lost=%u\r\n", (unsigned) count, (unsigned) m->rx_frames,
           (unsigned) m->tx_frames, (unsigned) m->fifo_lost);
    for (i = 0U; i < count; i++) {
        const co_master_frame_t* f = &m->fifo[(tail + i) % CO_MASTER_FIFO_LEN];
        printf("  id=%03X dlc=%u t=%u us  %02X %02X %02X %02X %02X %02X %02X %02X\r\n", (unsigned) f->id,
               (unsigned) f->dlc, (unsigned) f->ts_us, f->data[0], f->data[1], f->data[2], f->data[3],
               f->data[4], f->data[5], f->data[6], f->data[7]);
    }
}
