/*
 * usb_gs.c - gs_usb / candleLight USB 设备层（CherryUSB vendor class）
 *
 * 描述符、控制请求、IN/OUT 端点均为按 docs/gs_usb开发约定.md §3 与内核 7.0.0 gs_usb.c
 * 的协议事实独立实现。CherryUSB 为 Apache-2.0；本文件不含 GPL 源码。
 *
 * 端点：
 *   EP1 IN  0x81 bulk 512B：设备→主机，每传输恰好一帧（经典 24B / FD 80B，含 1 MHz 时间戳）
 *   EP2 OUT 0x02 bulk 512B：主机→设备，经典 20B / FD 76B（无时间戳）
 * 控制请求：wIndex = 通道号（HOST_FORMAT/DEVICE_CONFIG/TIMESTAMP 用 0）
 */
#include <string.h>
#include "board.h"
#include "usb_config.h"
#include "usbd_core.h"
#include "hpm_usb_drv.h"
#include "gs_usb.h"
#include "bridge.h"
#include "usb_gs.h"

/* ---------------- 描述符（事实数据，按协议自写） ---------------- */
#define GS_USB_CONFIG_SIZE (9U + 9U + 7U + 7U)   /* config + interface + 2 endpoint */

static const uint8_t gs_device_desc[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, GS_USB_VID, GS_USB_PID, 0x0100, 0x01)
};

static const uint8_t gs_config_desc_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(GS_USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, 500),
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_IN, 0x02, GS_USB_EP_MPS_HS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_OUT, 0x02, GS_USB_EP_MPS_HS, 0x00),
};

static const uint8_t gs_config_desc_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(GS_USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, 500),
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_IN, 0x02, GS_USB_EP_MPS_FS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_OUT, 0x02, GS_USB_EP_MPS_FS, 0x00),
};

static const uint8_t gs_qualifier_desc[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01)
};

static const uint8_t gs_other_speed_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(GS_USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, 500),
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_IN, 0x02, GS_USB_EP_MPS_FS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_OUT, 0x02, GS_USB_EP_MPS_FS, 0x00),
};

static const uint8_t gs_other_speed_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(GS_USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, 500),
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_IN, 0x02, GS_USB_EP_MPS_HS, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(GS_USB_EP_OUT, 0x02, GS_USB_EP_MPS_HS, 0x00),
};

static const char *gs_string_descs[] = {
    (const char[]) {0x09, 0x04},                 /* LangID 0x0409 */
    "HPMicro",                                   /* iManufacturer */
    "HPM5E31 gs_usb CANFD",                      /* iProduct */
    "HPM5E31-GSUSB-0001",                        /* iSerial（固定；芯片 UID 待后续替代） */
};

static const uint8_t *gs_device_desc_cb(uint8_t speed)
{
    (void) speed;
    return gs_device_desc;
}

static const uint8_t *gs_config_desc_cb(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? gs_config_desc_hs : gs_config_desc_fs;
}

static const uint8_t *gs_qualifier_cb(uint8_t speed)
{
    (void) speed;
    return gs_qualifier_desc;
}

static const uint8_t *gs_other_speed_cb(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? gs_other_speed_hs : gs_other_speed_fs;
}

static const char *gs_string_cb(uint8_t speed, uint8_t index)
{
    (void) speed;
    if (index >= (sizeof(gs_string_descs) / sizeof(gs_string_descs[0]))) {
        return NULL;
    }
    return gs_string_descs[index];
}

static const struct usb_descriptor gs_usb_descriptor = {
    .device_descriptor_callback = gs_device_desc_cb,
    .config_descriptor_callback = gs_config_desc_cb,
    .device_quality_descriptor_callback = gs_qualifier_cb,
    .other_speed_descriptor_callback = gs_other_speed_cb,
    .string_descriptor_callback = gs_string_cb,
};

/* ---------------- 缓冲区与状态 ---------------- */

#define IN_RING_SIZE (48U)
#define OUT_RING_SIZE (16U)

typedef struct {
    uint8_t len;
    uint8_t data[GS_USB_RX_FD_LEN];
} in_item_t;

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t s_ep_in_buf[GS_USB_RX_FD_LEN];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t s_ep_out_buf[GS_USB_EP_MPS_HS];

static in_item_t s_in_ring[IN_RING_SIZE];
static volatile uint32_t s_in_head;
static volatile uint32_t s_in_tail;
static volatile bool s_in_busy;
static volatile bool s_out_armed;
static volatile uint32_t s_in_drop;
static volatile uint32_t s_out_drop;
static volatile uint32_t s_in_sent;
static volatile bool s_configured;
static volatile uint32_t s_suspend_cnt;
static volatile uint32_t s_resume_cnt;
static volatile uint32_t s_reset_cnt;

static uint8_t s_host_format_ok;

/* 控制请求追踪（诊断用：确认内核是否真的给每个通道发 MODE/BITTIMING） */
#define CTRL_LOG_N (32U)
struct ctrl_log_ent {
    uint8_t req;
    uint8_t dir_in;
    uint16_t index;
    uint16_t length;
    uint32_t arg0;      /* MODE: mode；BITTIMING: brp */
    uint32_t arg1;      /* MODE: flags */
};
static struct ctrl_log_ent s_ctrl_log[CTRL_LOG_N];
static volatile uint32_t s_ctrl_log_head;
static volatile uint32_t s_ctrl_log_total;

static void ctrl_log_add(uint8_t req, uint8_t dir_in, uint16_t index, uint16_t len,
                         uint32_t a0, uint32_t a1)
{
    uint32_t i = s_ctrl_log_head % CTRL_LOG_N;
    s_ctrl_log[i].req = req;
    s_ctrl_log[i].dir_in = dir_in;
    s_ctrl_log[i].index = index;
    s_ctrl_log[i].length = len;
    s_ctrl_log[i].arg0 = a0;
    s_ctrl_log[i].arg1 = a1;
    s_ctrl_log_head++;
    s_ctrl_log_total++;
}

void gs_usb_ctrl_dump(void)
{
    uint32_t n = (s_ctrl_log_head < CTRL_LOG_N) ? s_ctrl_log_head : CTRL_LOG_N;
    printf("[ctrl] total=%lu (last %lu)\r\n", (unsigned long) s_ctrl_log_total, (unsigned long) n);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = (s_ctrl_log_head - n + k) % CTRL_LOG_N;
        printf("[ctrl] req=%u %s idx=%u len=%u arg=%08lX/%08lX\r\n",
               (unsigned) s_ctrl_log[i].req, s_ctrl_log[i].dir_in ? "IN " : "OUT",
               (unsigned) s_ctrl_log[i].index, (unsigned) s_ctrl_log[i].length,
               (unsigned long) s_ctrl_log[i].arg0, (unsigned long) s_ctrl_log[i].arg1);
    }
}
static bool s_berr_en[GS_USB_CHANNELS];
static uint32_t s_identify_mode;

/* 控制请求 IN 应答缓冲（长度按请求） */
static struct gs_device_config s_cfg_dev;
static struct gs_device_bt_const s_cfg_bt;
static struct gs_device_bt_const_ext s_cfg_bt_ext;
static uint32_t s_cfg_ts32;   /* TIMESTAMP 请求只回 4 B */
static struct gs_device_state s_cfg_state;

/* ---------------- 端点状态机 ---------------- */

static int in_ring_push(const uint8_t *data, uint32_t len)
{
    uint32_t next = (s_in_head + 1U) % IN_RING_SIZE;
    if ((data == NULL) || (len == 0U) || (len > GS_USB_RX_FD_LEN) || (next == s_in_tail)) {
        s_in_drop++;
        return -1;
    }
    s_in_ring[s_in_head].len = (uint8_t) len;
    memcpy(s_in_ring[s_in_head].data, data, len);
    s_in_head = next;
    return 0;
}

static void in_kick(void)
{
    uint32_t len;

    if (!s_configured || s_in_busy || (s_in_tail == s_in_head)) {
        return;
    }
    len = s_in_ring[s_in_tail].len;
    memcpy(s_ep_in_buf, s_in_ring[s_in_tail].data, len);
    s_in_busy = true;
    if (usbd_ep_start_write(0, GS_USB_EP_IN, s_ep_in_buf, len) != 0) {
        s_in_busy = false;
        s_in_drop++;
        return;
    }
    s_in_tail = (s_in_tail + 1U) % IN_RING_SIZE;
    s_in_sent++;
}

static void out_arm(void)
{
    if (s_configured && !s_out_armed) {
        s_out_armed = true;
        (void) usbd_ep_start_read(0, GS_USB_EP_OUT, s_ep_out_buf, sizeof(s_ep_out_buf));
    }
}

static void gs_ep_in_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void) busid;
    (void) ep;
    (void) nbytes;
    s_in_busy = false;   /* 主循环 gs_usb_usb_poll() 会继续发下一帧 */
}

static void gs_ep_out_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void) busid;
    (void) ep;
    s_out_armed = false;
    if ((nbytes >= GS_USB_FRAME_HDR_LEN) && (nbytes <= GS_USB_TX_FD_LEN)) {
        gs_bridge_tx_from_host(s_ep_out_buf, nbytes);
    } else if (nbytes > 0U) {
        s_out_drop++;
    }
    out_arm();
}

static void gs_bus_event(uint8_t busid, uint8_t event, void *arg)
{
    (void) busid;
    (void) arg;
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        s_configured = true;
        s_in_busy = false;
        out_arm();
        break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
    case USBD_EVENT_DEINIT:
        s_reset_cnt++;
        s_configured = false;
        s_in_busy = false;
        s_out_armed = false;
        break;
    case USBD_EVENT_SUSPEND:
        /* 主机 autosuspend/LPM：Linux usbcore 默认 autosuspend=2s，空闲会挂起。
         * 固件必须能安全进入/退出；否则 host 会判 detach（本项目实测空闲 84.8 s 掉线，
         * 根因即主机 USB autosuspend，见 README 与第40章“易错点”。） */
        s_suspend_cnt++;
        printf("[usb] SUSPEND #%lu\r\n", (unsigned long) s_suspend_cnt);
        break;
    case USBD_EVENT_RESUME:
        s_resume_cnt++;
        /* 恢复后端点可能处于未知状态：清 busy、重新 arm OUT 读，避免 host URB 卡死 */
        s_in_busy = false;
        s_out_armed = false;
        out_arm();
        printf("[usb] RESUME #%lu re-armed (configured=%u)\r\n",
               (unsigned long) s_resume_cnt, s_configured ? 1U : 0U);
        break;
    default:
        break;
    }
}

/* ---------------- 控制请求 ---------------- */

static int gs_vendor_request(uint8_t busid, struct usb_setup_packet *setup,
                             uint8_t **data, uint32_t *len)
{
    uint8_t req = setup->bRequest;
    /* 内核 7.0.0 gs_usb 驱动把"通道号"放在 wValue（不是 wIndex！），
     * 例如 usb_control_msg_send(..., GS_USB_BREQ_MODE, ..., dev->channel, 0, ...)；
     * HOST_FORMAT 的 wValue=1、wIndex=接口号，与本分支无关。 */
    uint8_t ch = (uint8_t) (setup->wValue & 0xFFU);
    uint8_t dir_in = (setup->bmRequestType & 0x80U) ? 1U : 0U;
    uint32_t a0 = 0U, a1 = 0U;

    (void) busid;

    if ((req == GS_USB_BREQ_MODE) && !dir_in && (*data != NULL) && (*len >= 8U)) {
        struct gs_device_mode m;
        memcpy(&m, *data, sizeof(m));
        a0 = m.mode;
        a1 = m.flags;
    } else if ((req == GS_USB_BREQ_BITTIMING) && !dir_in && (*data != NULL) && (*len >= 8U)) {
        a0 = ((uint32_t *) (*data))[0];
        a1 = ((uint32_t *) (*data))[4];
    }
    ctrl_log_add(req, dir_in, (uint16_t) setup->wValue, (uint16_t) setup->wLength, a0, a1);

    if (dir_in) {
        const void *resp = NULL;
        uint32_t resp_len = 0U;

        switch (req) {
        case GS_USB_BREQ_BT_CONST:
            if ((ch >= GS_USB_CHANNELS) || (s_host_format_ok == 0U)) {
                return -1;
            }
            gs_usb_fill_bt_const(&s_cfg_bt);
            resp = &s_cfg_bt;
            resp_len = sizeof(s_cfg_bt);
            break;
        case GS_USB_BREQ_BT_CONST_EXT:
            if ((ch >= GS_USB_CHANNELS) || (s_host_format_ok == 0U)) {
                return -1;
            }
            gs_usb_fill_bt_const_ext(&s_cfg_bt_ext);
            resp = &s_cfg_bt_ext;
            resp_len = sizeof(s_cfg_bt_ext);
            break;
        case GS_USB_BREQ_DEVICE_CONFIG:
            gs_usb_fill_device_config(&s_cfg_dev);
            resp = &s_cfg_dev;
            resp_len = sizeof(s_cfg_dev);
            break;
        case GS_USB_BREQ_TIMESTAMP:
            /* 内核 gs_usb_get_timestamp() 只读 4 字节（单个 le32，1 MHz 计数低 32 位），
             * 高 32 位由内核用 CYCLECOUNTER 机制每 1800 s 扩展；不能回 12 字节结构！ */
            s_cfg_ts32 = gs_bridge_now_us();
            resp = &s_cfg_ts32;
            resp_len = sizeof(s_cfg_ts32);
            break;
        case GS_USB_BREQ_GET_STATE:
            if (ch >= GS_USB_CHANNELS) {
                return -1;
            }
            gs_bridge_can_get_state(ch, &s_cfg_state);
            resp = &s_cfg_state;
            resp_len = sizeof(s_cfg_state);
            break;
        default:
            /* TERMINATION / USER_ID / 未知请求一律 STALL */
            return -1;
        }
        if (resp_len > setup->wLength) {
            resp_len = setup->wLength;
        }
        *data = (uint8_t *) resp;
        *len = resp_len;
        return 0;
    }

    /* OUT 方向 */
    switch (req) {
    case GS_USB_BREQ_HOST_FORMAT:
        if ((*len < sizeof(struct gs_host_config)) || (*data == NULL)) {
            return -1;
        }
        {
            struct gs_host_config hc;
            memcpy(&hc, *data, sizeof(hc));
            s_host_format_ok = (hc.byte_order == GS_USB_BYTE_ORDER_BEEF) ? 1U : 1U;
        }
        return 0;
    case GS_USB_BREQ_BITTIMING:
    case GS_USB_BREQ_DATA_BITTIMING:
        if ((ch >= GS_USB_CHANNELS) || (*len < sizeof(struct gs_device_bittiming)) || (*data == NULL)) {
            return -1;
        }
        {
            struct gs_device_bittiming bt;
            memcpy(&bt, *data, sizeof(bt));
            gs_bridge_can_store_btim(ch, (req == GS_USB_BREQ_DATA_BITTIMING) ? true : false, &bt);
        }
        return 0;
    case GS_USB_BREQ_MODE:
        if ((ch >= GS_USB_CHANNELS) || (*len < sizeof(struct gs_device_mode)) || (*data == NULL)) {
            return -1;
        }
        {
            struct gs_device_mode m;
            memcpy(&m, *data, sizeof(m));
            printf("[gs] MODE ch=%u mode=%lu flags=0x%lX\r\n", (unsigned) ch,
                   (unsigned long) m.mode, (unsigned long) m.flags);
            (void) gs_bridge_can_mode(ch, m.mode, m.flags);
        }
        return 0;
    case GS_USB_BREQ_BERR:
        if ((*len < 4U) || (*data == NULL)) {
            return -1;
        }
        if (ch < GS_USB_CHANNELS) {
            uint32_t en = 0U;
            memcpy(&en, *data, sizeof(en));
            s_berr_en[ch] = (en != 0U);
            gs_bridge_set_berr(ch, (en != 0U));
        }
        return 0;
    case GS_USB_BREQ_IDENTIFY:
        if ((*len < 4U) || (*data == NULL)) {
            return -1;
        }
        memcpy(&s_identify_mode, *data, sizeof(s_identify_mode));
        gs_bridge_set_identify(s_identify_mode);
        return 0;
    default:
        return -1;
    }
}

/* ---------------- 对外 API ---------------- */

static struct usbd_endpoint gs_ep_out = { .ep_addr = GS_USB_EP_OUT, .ep_cb = gs_ep_out_handler };
static struct usbd_endpoint gs_ep_in = { .ep_addr = GS_USB_EP_IN, .ep_cb = gs_ep_in_handler };
static struct usbd_interface gs_intf;

void gs_usb_usb_init(void)
{
    s_in_head = 0U;
    s_in_tail = 0U;
    s_in_busy = false;
    s_out_armed = false;
    s_configured = false;

    board_init_usb((USB_Type *) CONFIG_HPM_USBD_BASE);
    usb_phy_using_internal_vbus((USB_Type *) CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);

    usbd_desc_register(0, &gs_usb_descriptor);
    gs_intf.class_interface_handler = NULL;
    gs_intf.class_endpoint_handler = NULL;
    gs_intf.vendor_handler = gs_vendor_request;
    gs_intf.notify_handler = NULL;
    usbd_add_interface(0, &gs_intf);
    usbd_add_endpoint(0, &gs_ep_out);
    usbd_add_endpoint(0, &gs_ep_in);
    usbd_initialize(0, CONFIG_HPM_USBD_BASE, gs_bus_event);

#if GS_USB_SET_SDIS
    /* A/B 实验：roboparty 参考实现设置 USBMODE.SDIS；本项目默认打开，
     * 若发现影响 suspend/resume 可置 0 对比。 */
    ((USB_Type *) CONFIG_HPM_USBD_BASE)->USBMODE |= USB_USBMODE_SDIS_MASK;
#endif
}

bool gs_usb_usb_ready(void)
{
    return s_configured;
}

int gs_usb_usb_send(const uint8_t *frame, uint32_t len)
{
    return in_ring_push(frame, len);
}

void gs_usb_usb_poll(void)
{
    in_kick();
    out_arm();
}

void gs_usb_usb_stats(uint32_t *in_drop, uint32_t *out_drop, uint32_t *in_sent)
{
    if (in_drop != NULL) {
        *in_drop = s_in_drop;
    }
    if (out_drop != NULL) {
        *out_drop = s_out_drop;
    }
    if (in_sent != NULL) {
        *in_sent = s_in_sent;
    }
}

void gs_usb_usb_pm_stats(uint32_t *suspend, uint32_t *resume, uint32_t *reset)
{
    if (suspend != NULL) {
        *suspend = s_suspend_cnt;
    }
    if (resume != NULL) {
        *resume = s_resume_cnt;
    }
    if (reset != NULL) {
        *reset = s_reset_cnt;
    }
}