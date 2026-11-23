/*
 * xcp_core.c —— XCP 协议层（命令解析 + DAQ 引擎），与传输层无关
 *
 * 数据通路：
 *   CTO(命令) 传输层 poll() -> xcp_rx_cto() -> dispatch -> send_res()/send_err() -> 传输层 send_dto()
 *   DAQ 采样  xcp_event()（中断里打包 ODT）-> 环形队列 -> xcp_poll()（主循环发送）
 *
 * 为什么采样和发送分开：1 kHz 事件里如果直接调 CAN 发送，一旦 TX FIFO 满会在中断里
 * 自旋；而把发送放主循环又会让"帧的时间戳"失真。折中做法是中断里按事件时刻取时间戳
 * 并打包（数据一致），主循环发送。丢帧只影响吞吐统计，不影响时间戳语义。
 */
#include "xcp.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部状态                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  bit_off;               /* 0xFF = 字节对齐整块拷贝 */
    uint8_t  size;                  /* 字节数 1..XCP_MAX_DTO-1 */
    uint8_t  ext;
    uint32_t addr;
} xcp_odt_entry_t;

typedef struct {
    uint8_t  in_use;
    uint8_t  state;
    uint8_t  mode;
    uint8_t  event;
    uint8_t  prescaler;
    uint8_t  prescaler_cnt;
    uint8_t  priority;
    uint8_t  first_pid;
    uint8_t  odt_count;
    uint8_t  odt_entry_count[XCP_MAX_ODT_PER_LIST];
    xcp_odt_entry_t entry[XCP_MAX_ODT_PER_LIST][XCP_MAX_ENTRY_PER_ODT];
} xcp_daq_list_t;

typedef struct {
    uint16_t len;                   /* >255 B 的 DTO（XCPonEth 构型） */
    uint8_t data[XCP_MAX_DTO];
} xcp_tx_frame_t;

static const xcp_cfg_t *s_cfg;
static const xcp_transport_ops_t *s_tp;

static bool     s_connected;
static uint32_t s_mta;
static uint8_t  s_mta_ext;
static uint8_t  s_last_error;
static uint8_t  s_state_number;
static uint16_t s_session_cfg;

static uint8_t  s_ptr_list, s_ptr_odt, s_ptr_entry;
static uint8_t  s_pag_segment, s_pag_page;
static uint8_t  s_protection;       /* 仍被锁的资源掩码 */
static bool     s_unlocked;
static uint8_t  s_seed[4];          /* 运行期种子（可由 xcp_set_seed 改） */
static uint8_t  s_seed_len;

static xcp_daq_list_t s_daq[XCP_MAX_DAQ_LISTS];
static uint8_t  s_daq_allocated;    /* ALLOC_DAQ 分配到的列表数 */
static uint8_t  s_daq_pid_total;    /* 已分配 ODT 总数（PID 上限） */

static xcp_tx_frame_t s_q[XCP_DAQ_TX_QUEUE_LEN];
static volatile uint16_t s_q_head;  /* 写入位置（事件中断） */
static volatile uint16_t s_q_tail;  /* 读出位置（主循环） */
static bool s_ovl_reported;         /* DAQ 过载事件已上报（防止刷屏） */

static xcp_stats_t s_stats;

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) (v >> 8);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) ((v >> 8) & 0xFFu);
    p[2] = (uint8_t) ((v >> 16) & 0xFFu);
    p[3] = (uint8_t) ((v >> 24) & 0xFFu);
}

static uint32_t xcp_now_us(void)
{
    return (s_cfg != NULL && s_cfg->now_us != NULL) ? s_cfg->now_us() : 0u;
}

/* 内存区域白名单：读/写都要过，越界直接报错而不是让总线出错 */
static bool mem_within(uint32_t addr, uint32_t len, bool write)
{
    if (s_cfg == NULL) {
        return false;
    }
    if (len == 0u) {
        return true;
    }
    if (addr + len < addr) {            /* 32 位回绕 */
        return false;
    }
    for (uint8_t i = 0; i < s_cfg->region_count; i++) {
        const xcp_mem_region_t *r = &s_cfg->regions[i];
        if (addr < r->start || (addr + len) > r->end) {
            continue;
        }
        uint8_t need = write ? XCP_MEM_WRITE : XCP_MEM_READ;
        if ((r->flags & need) == need) {
            return true;
        }
    }
    s_stats.mem_denied++;
    return false;
}

/* ------------------------------------------------------------------ */
/* 响应发送                                                            */
/* ------------------------------------------------------------------ */

static void tp_send(const uint8_t *buf, uint16_t len)
{
    if (s_tp != NULL && s_tp->send_dto != NULL) {
        (void) s_tp->send_dto(buf, len);
    }
}

static void send_res(const uint8_t *payload, uint16_t len)
{
    uint8_t buf[XCP_MAX_DTO];

    if (len > (XCP_MAX_DTO - 1u)) {
        len = XCP_MAX_DTO - 1u;
    }
    buf[0] = XCP_PID_RES;
    if (len > 0u) {
        memcpy(&buf[1], payload, len);
    }
    tp_send(buf, (uint16_t) (len + 1u));
    s_stats.res_tx++;
}

static void send_err(uint8_t code)
{
    uint8_t buf[3];

    s_last_error = code;
    buf[0] = XCP_PID_ERR;
    buf[1] = code;
    buf[2] = 0u;
    tp_send(buf, 3u);
    s_stats.err_tx++;
}

static void send_ev(uint8_t code, const uint8_t *data, uint16_t len)
{
    uint8_t buf[XCP_MAX_DTO];

    if (len > (XCP_MAX_DTO - 2u)) {
        len = XCP_MAX_DTO - 2u;
    }
    buf[0] = XCP_PID_EV;
    buf[1] = code;
    if (len > 0u) {
        memcpy(&buf[2], data, len);
    }
    tp_send(buf, (uint16_t) (len + 2u));
}

static void send_ok(void)
{
    send_res(NULL, 0u);
}

/* ------------------------------------------------------------------ */
/* DAQ 基础操作                                                        */
/* ------------------------------------------------------------------ */

static void daq_clear_all(void)
{
    memset(s_daq, 0, sizeof(s_daq));
    s_daq_allocated = 0u;
    s_daq_pid_total = 0u;
    s_ptr_list = 0u;
    s_ptr_odt = 0u;
    s_ptr_entry = 0u;
}

/* PID = 绝对 ODT 号，按列表分配顺序连续编号 */
static void daq_recalc_pids(void)
{
    uint16_t pid = 0u;

    for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
        if (s_daq[i].in_use == 0u) {
            s_daq[i].first_pid = 0xFFu;
            continue;
        }
        /* PID 0x00..0xFB 可用于 DAQ（0xFC..0xFF 留给 SERV/EV/ERR/RES） */
        if ((uint16_t) (pid + s_daq[i].odt_count) > 0xFBu) {
            s_daq[i].odt_count = (uint8_t) (0xFBu - pid);
        }
        s_daq[i].first_pid = (uint8_t) pid;
        pid = (uint16_t) (pid + s_daq[i].odt_count);
    }
    s_daq_pid_total = (uint8_t) pid;
}

static bool daq_list_ok(uint8_t list)
{
    return (list < XCP_MAX_DAQ_LISTS) && (s_daq[list].in_use != 0u);
}

static bool daq_any_running(void)
{
    for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
        if ((s_daq[i].state & XCP_DAQ_STATE_RUNNING) != 0u) {
            return true;
        }
    }
    return false;
}

static bool q_push(const uint8_t *data, uint16_t len)
{
    uint16_t next = (uint16_t) ((s_q_head + 1u) % XCP_DAQ_TX_QUEUE_LEN);

    if (next == s_q_tail) {
        s_stats.daq_dropped++;
        return false;
    }
    s_q[s_q_head].len = len;
    memcpy(s_q[s_q_head].data, data, len);
    s_q_head = next;
    return true;
}

/* 单个条目打包：bit_off==0xFF 整块拷贝，否则按位域取值（A2L BIT_MASK 场景） */
static void odt_pack_entry(uint8_t *dst, const xcp_odt_entry_t *en)
{
    const uint8_t *src = (const uint8_t *) (uintptr_t) en->addr;

    if (en->bit_off == 0xFFu) {
        memcpy(dst, src, en->size);
    } else {
        uint32_t v = *(const uint32_t *) (uintptr_t) en->addr;
        v >>= en->bit_off;
        for (uint8_t i = 0; i < en->size; i++) {
            dst[i] = (uint8_t) ((v >> (8u * i)) & 0xFFu);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 命令实现                                                            */
/* ------------------------------------------------------------------ */

static void cmd_connect(const uint8_t *c, uint16_t len)
{
    uint8_t res[8];

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (s_connected) {              /* 已连接时重复 CONNECT 按规范静默忽略 */
        s_stats.cmd_ignored++;
        return;
    }
    s_connected = true;
    s_state_number++;
    s_session_cfg = 0u;
    s_mta = 0u;
    s_mta_ext = 0u;
    daq_clear_all();
    s_protection = (s_seed_len < 0u) ? (uint8_t) (XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG) : 0u;
    s_unlocked = (s_protection == 0u);

    res[0] = (uint8_t) (XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG);
    res[1] = (uint8_t) (XCP_CMB_OPTIONAL | XCP_CMB_BYTE_ORDER_INTEL | XCP_CMB_ADDR_GRAN_BYTE);
    res[2] = XCP_MAX_CTO;
    wr_u16(&res[3], XCP_MAX_DTO);
    res[5] = 0x14u;                 /* 协议层版本：XCP 1.4 语义（无 PACKED_MODE） */
    res[6] = 0x01u;                 /* 传输层版本：XCPonCAN 1.x */
    send_res(res, 7u);

    if (s_cfg->on_connect != NULL) {
        s_cfg->on_connect();
    }
}

static void cmd_disconnect(void)
{
    daq_clear_all();
    s_connected = false;
    s_state_number++;
    send_ok();
    if (s_cfg->on_disconnect != NULL) {
        s_cfg->on_disconnect();
    }
}

static void cmd_get_status(void)
{
    uint8_t res[8];

    wr_u16(&res[0], daq_any_running() ? XCP_SS_DAQ_RUNNING : 0u);
    res[2] = s_protection;
    res[3] = s_state_number;
    wr_u16(&res[4], s_session_cfg);
    send_res(res, 6u);
}

static void cmd_get_comm_mode_info(void)
{
    uint8_t res[7];

    memset(res, 0, sizeof(res));
    res[1] = 0x00u;                 /* 无主站块传输、无 interleaved 模式 */
    res[6] = 0x01u;                 /* XCP driver 版本号 */
    send_res(res, 7u);
}

static void cmd_get_id(const uint8_t *c, uint16_t len)
{
    uint8_t res[7];
    const char *id = (s_cfg->id_string != NULL) ? s_cfg->id_string : "HPM5E31-XCP";
    uint32_t n = (uint32_t) strlen(id);

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    res[0] = c[1];                  /* 回显 mode（0 = ASCII） */
    res[1] = 0u;
    res[2] = 0u;
    wr_u32(&res[3], n);
    send_res(res, 7u);

    /* 规范要求：GET_ID 之后把 MTA 指向标识串，主机用 UPLOAD 取回 */
    s_mta = (uint32_t) (uintptr_t) id;
    s_mta_ext = 0u;
}

static void cmd_set_request(const uint8_t *c, uint16_t len)
{
    uint8_t res[1];

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    s_session_cfg = (uint16_t) (c[2] | ((uint16_t) c[3] << 8));
    res[0] = 0u;                    /* 无 EEPROM/Flash 持久化：接受但不落盘 */
    send_res(res, 1u);
}

static void cmd_get_seed(const uint8_t *c, uint16_t len)
{
    uint8_t res[8];

    if (len < 3u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (s_seed_len == 0u) {
        res[0] = 0u;                /* 长度 0 = 该资源不受保护 */
        send_res(res, 1u);
        return;
    }
    if ((c[2] & s_protection) == 0u) {
        res[0] = 0u;
        send_res(res, 1u);
        return;
    }
    res[0] = s_seed_len;
    memcpy(&res[1], s_seed, s_seed_len);
    send_res(res, (uint16_t) (1u + s_seed_len));
}

static void cmd_unlock(const uint8_t *c, uint16_t len)
{
    uint8_t res[1];
    uint8_t n;

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    n = c[1];
    if (s_seed_len == 0u) {
        res[0] = 0u;
        send_res(res, 1u);
        return;
    }
    if ((n != s_seed_len) || (len < (uint16_t) (2u + n))) {
        send_err(XCP_ERR_SEQUENCE);
        return;
    }
    /* 演示用 key 算法：key[i] = seed[i] ^ 0xA5。真实项目应放 seed/key DLL 或查表 */
    for (uint8_t i = 0; i < n; i++) {
        if (c[2u + i] != (uint8_t) (s_seed[i] ^ 0xA5u)) {
            send_err(XCP_ERR_ACCESS_LOCKED);
            return;
        }
    }
    s_unlocked = true;
    s_protection = 0u;
    res[0] = 0u;
    send_res(res, 1u);
}

static void cmd_set_mta(const uint8_t *c, uint16_t len)
{
    if (len < 7u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    s_mta_ext = c[2];
    s_mta = rd_u32(&c[3]);
    send_ok();
}

static void cmd_upload(const uint8_t *c, uint16_t len)
{
    uint8_t res[XCP_MAX_DTO];
    uint8_t n;

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    n = c[1];
    if ((uint16_t) n > (uint16_t) (XCP_MAX_DTO - 1u)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    if (!mem_within(s_mta, n, false)) {
        send_err(XCP_ERR_ACCESS_DENIED);
        return;
    }
    memcpy(res, (const void *) (uintptr_t) s_mta, n);
    s_mta += n;
    send_res(res, n);
}

static void cmd_short_upload(const uint8_t *c, uint16_t len)
{
    uint8_t res[XCP_MAX_DTO];
    uint8_t n;

    if (len < 8u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    n = c[2];
    if ((uint16_t) n > (uint16_t) (XCP_MAX_DTO - 1u)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    uint32_t addr = rd_u32(&c[4]);
    if (!mem_within(addr, n, false)) {
        send_err(XCP_ERR_ACCESS_DENIED);
        return;
    }
    memcpy(res, (const void *) (uintptr_t) addr, n);
    send_res(res, n);
}

static void cmd_download(const uint8_t *c, uint16_t len)
{
    uint8_t n;

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    n = c[1];
    if ((uint16_t) (2u + n) > len) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (!s_unlocked) {
        send_err(XCP_ERR_ACCESS_LOCKED);
        return;
    }
    if (!mem_within(s_mta, n, true)) {
        send_err(XCP_ERR_WRITE_PROTECTED);
        return;
    }
    memcpy((void *) (uintptr_t) s_mta, &c[2], n);
    s_mta += n;
    send_ok();
}

static void cmd_modify_bits(const uint8_t *c, uint16_t len)
{
    uint32_t v, mask, x;

    if (len < 6u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (!s_unlocked) {
        send_err(XCP_ERR_ACCESS_LOCKED);
        return;
    }
    if (!mem_within(s_mta, 4u, true) || !mem_within(s_mta, 4u, false)) {
        send_err(XCP_ERR_WRITE_PROTECTED);
        return;
    }
    /* 规范公式：MEM = (MEM & ~(((uint32)(~AND)) << SHIFT)) ^ ((uint32)XOR << SHIFT) */
    uint8_t shift = c[1];
    uint16_t and_mask = rd_u16(&c[2]);
    uint16_t xor_mask = rd_u16(&c[4]);
    mask = ((uint32_t) ((uint16_t) ~and_mask)) << shift;
    x = ((uint32_t) xor_mask) << shift;
    v = *(volatile uint32_t *) (uintptr_t) s_mta;
    v = (v & ~mask) ^ x;
    *(volatile uint32_t *) (uintptr_t) s_mta = v;
    send_ok();
}

/* ---------------- 标定页（单段单页 RAM） ---------------- */

static void cmd_get_pag_processor_info(void)
{
    uint8_t res[2];

    res[0] = 1u;                    /* MAX_SEGMENT */
    res[1] = 0u;                    /* PAG_PROPERTIES：不支持 FREEZE */
    send_res(res, 2u);
}

static void cmd_get_segment_info(const uint8_t *c, uint16_t len)
{
    uint8_t res[8];
    uint8_t mode;

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    mode = c[1];
    if (c[2] != 0u) {               /* 只有 segment 0 */
        send_err(XCP_ERR_SEGMENT_NOT_VALID);
        return;
    }
    if (mode == 1u) {
        res[0] = 1u;                /* MAX_PAGES */
        res[1] = 0u;                /* ADDRESS_EXTENSION */
        res[2] = 0u;                /* MAX_MAPPING */
        res[3] = 0u;                /* COMPRESSION */
        res[4] = 0u;                /* ENCRYPTION */
        send_res(res, 5u);
    } else if (mode == 0u) {
        wr_u32(&res[0], 0u);        /* 基本段信息保留 */
        send_res(res, 4u);
    } else {
        wr_u32(&res[0], 0u);        /* mapping 信息保留 */
        send_res(res, 4u);
    }
}

static void cmd_get_page_info(const uint8_t *c, uint16_t len)
{
    uint8_t res[2];

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if ((c[2] != 0u) || (c[3] != 0u)) {
        send_err(XCP_ERR_PAGE_NOT_VALID);
        return;
    }
    /* ECU 与 XCP 都能读写该页（RAM 标定页） */
    res[0] = 0x3Fu;
    res[1] = 0u;
    send_res(res, 2u);
}

static void cmd_set_cal_page(const uint8_t *c, uint16_t len)
{
    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if ((c[2] != 0u) || (c[3] != 0u)) {
        send_err(XCP_ERR_PAGE_NOT_VALID);
        return;
    }
    s_pag_segment = c[2];
    s_pag_page = c[3];
    send_ok();
}

static void cmd_get_cal_page(const uint8_t *c, uint16_t len)
{
    uint8_t res[2];

    if (len < 3u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    (void) c;
    res[0] = 0u;                    /* 保留 */
    res[1] = s_pag_page;
    send_res(res, 2u);
}

/* ---------------- DAQ ---------------- */

static void cmd_free_daq(void)
{
    daq_clear_all();
    send_ok();
}

static void cmd_alloc_daq(const uint8_t *c, uint16_t len)
{
    uint16_t count;

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (c[1] != 0u) {               /* 只支持顺序分配模式 */
        send_err(XCP_ERR_MODE_NOT_VALID);
        return;
    }
    count = rd_u16(&c[2]);
    if (count > XCP_MAX_DAQ_LISTS) {
        send_err(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }
    daq_clear_all();
    for (uint16_t i = 0; i < count; i++) {
        s_daq[i].in_use = 1u;
    }
    s_daq_allocated = (uint8_t) count;
    daq_recalc_pids();
    send_ok();
}

static void cmd_alloc_odt(const uint8_t *c, uint16_t len)
{
    uint8_t list, odt_count;

    if (len < 5u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (c[1] != 0u) {
        send_err(XCP_ERR_MODE_NOT_VALID);
        return;
    }
    list = c[2];                    /* DAQ_LIST_NUMBER 低字节（本实现最多 4 个列表） */
    odt_count = c[4];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    if (odt_count > XCP_MAX_ODT_PER_LIST) {
        send_err(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }
    s_daq[list].odt_count = odt_count;
    for (uint8_t i = 0; i < XCP_MAX_ODT_PER_LIST; i++) {
        s_daq[list].odt_entry_count[i] = 0u;
    }
    daq_recalc_pids();
    send_ok();
}

static void cmd_alloc_odt_entry(const uint8_t *c, uint16_t len)
{
    uint8_t list, odt, entries;

    /* CRO: cmd, mode, DAQ_LIST_NUMBER(2), ODT_NUMBER, ODT_ENTRIES_COUNT = 6 字节 */
    if (len < 6u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (c[1] != 0u) {
        send_err(XCP_ERR_MODE_NOT_VALID);
        return;
    }
    list = c[2];
    odt = c[4];
    entries = c[5];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    if ((odt >= s_daq[list].odt_count) || (entries > XCP_MAX_ENTRY_PER_ODT)) {
        send_err(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }
    s_daq[list].odt_entry_count[odt] = entries;
    send_ok();
}

static void cmd_set_daq_ptr(const uint8_t *c, uint16_t len)
{
    uint8_t list, odt, entry;

    if (len < 6u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    list = c[2];
    odt = c[4];
    entry = c[5];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    if ((odt >= s_daq[list].odt_count) || (entry > XCP_MAX_ENTRY_PER_ODT)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    s_ptr_list = list;
    s_ptr_odt = odt;
    s_ptr_entry = entry;
    send_ok();
}

/* 校验一个 ODT 的条目总数不超过 DTO 载荷 */
static bool odt_capacity_ok(const xcp_daq_list_t *l, uint8_t odt)
{
    uint16_t need = 0u;
    bool ts = ((l->mode & XCP_DAQ_MODE_TIMESTAMP) != 0u) && (odt == 0u);

    if (ts) {
        need = 4u;
    }
    for (uint8_t i = 0; i < l->odt_entry_count[odt]; i++) {
        need = (uint16_t) (need + l->entry[odt][i].size);
    }
    return (need <= (uint16_t) (XCP_MAX_DTO - 1u));
}

static void cmd_write_daq(const uint8_t *c, uint16_t len)
{
    xcp_daq_list_t *l;
    uint8_t bit_off, size, ext;

    if (len < 8u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    if (s_ptr_list >= XCP_MAX_DAQ_LISTS) {
        send_err(XCP_ERR_SEQUENCE);
        return;
    }
    l = &s_daq[s_ptr_list];
    if ((l->in_use == 0u) || (s_ptr_odt >= l->odt_count) || (s_ptr_entry >= XCP_MAX_ENTRY_PER_ODT)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    bit_off = c[1];
    size = c[2];
    ext = c[3];
    uint32_t addr = rd_u32(&c[4]);
    if ((size == 0u) || ((uint16_t) size > (uint16_t) (XCP_MAX_DTO - 1u))) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    if (!mem_within(addr, (bit_off == 0xFFu) ? size : 4u, false)) {
        send_err(XCP_ERR_ACCESS_DENIED);
        return;
    }
    l->entry[s_ptr_odt][s_ptr_entry].bit_off = bit_off;
    l->entry[s_ptr_odt][s_ptr_entry].size = size;
    l->entry[s_ptr_odt][s_ptr_entry].ext = ext;
    l->entry[s_ptr_odt][s_ptr_entry].addr = addr;
    if (s_ptr_entry >= l->odt_entry_count[s_ptr_odt]) {
        l->odt_entry_count[s_ptr_odt] = (uint8_t) (s_ptr_entry + 1u);
    }
    if (!odt_capacity_ok(l, s_ptr_odt)) {
        send_err(XCP_ERR_DAQ_CONFIG);
        return;
    }
    /* 按规范 WRITE_DAQ 成功后指针指向下一个条目 */
    if (s_ptr_entry < XCP_MAX_ENTRY_PER_ODT) {
        s_ptr_entry++;
    }
    send_ok();
}

static void cmd_read_daq(void)
{
    uint8_t res[8];
    xcp_daq_list_t *l;

    if ((s_ptr_list >= XCP_MAX_DAQ_LISTS) || (s_daq[s_ptr_list].in_use == 0u) ||
        (s_ptr_odt >= s_daq[s_ptr_list].odt_count) || (s_ptr_entry >= XCP_MAX_ENTRY_PER_ODT)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    l = &s_daq[s_ptr_list];
    res[0] = l->entry[s_ptr_odt][s_ptr_entry].bit_off;
    res[1] = l->entry[s_ptr_odt][s_ptr_entry].size;
    res[2] = l->entry[s_ptr_odt][s_ptr_entry].ext;
    wr_u32(&res[3], l->entry[s_ptr_odt][s_ptr_entry].addr);
    send_res(res, 7u);
}

static void cmd_clear_daq_list(const uint8_t *c, uint16_t len)
{
    uint8_t list;

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    list = c[2];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    for (uint8_t o = 0; o < XCP_MAX_ODT_PER_LIST; o++) {
        s_daq[list].odt_entry_count[o] = 0u;
    }
    s_daq[list].mode = 0u;
    s_daq[list].state = 0u;
    send_ok();
}

static void cmd_set_daq_list_mode(const uint8_t *c, uint16_t len)
{
    xcp_daq_list_t *l;
    uint8_t list;

    if (len < 8u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    list = c[2];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    l = &s_daq[list];
    if ((c[1] & XCP_DAQ_MODE_PID_OFF) != 0u) {
        send_err(XCP_ERR_MODE_NOT_VALID);   /* 本实现不支持 PID_OFF */
        return;
    }
    if ((c[1] & XCP_DAQ_MODE_STIM) != 0u) {
        send_err(XCP_ERR_MODE_NOT_VALID);   /* 不支持 STIM */
        return;
    }
    l->mode = c[1];
    l->event = c[4];                /* EVENT_CHANNEL_NUMBER 低字节（CRO: cmd,mode,list_lo,list_hi,evt_lo,evt_hi,presc,prio） */
    l->prescaler = c[6];
    l->priority = c[7];
    l->prescaler_cnt = 0u;
    /* 打开时间戳后 ODT0 只剩 3 字节，需要重新校验已有的打包 */
    for (uint8_t o = 0; o < l->odt_count; o++) {
        if (!odt_capacity_ok(l, o)) {
            send_err(XCP_ERR_DAQ_CONFIG);
            return;
        }
    }
    send_ok();
}

static void cmd_get_daq_list_mode(const uint8_t *c, uint16_t len)
{
    uint8_t res[8];
    xcp_daq_list_t *l;
    uint8_t list;

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    list = c[2];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    l = &s_daq[list];
    res[0] = l->mode;
    res[1] = 0u;
    res[2] = 0u;
    wr_u16(&res[3], l->event);
    res[5] = l->prescaler;
    res[6] = l->priority;
    send_res(res, 7u);
}

static void cmd_start_stop_daq_list(const uint8_t *c, uint16_t len)
{
    uint8_t res[1];
    xcp_daq_list_t *l;
    uint8_t mode, list;

    if (len < 4u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    mode = c[1];
    list = c[2];
    if (!daq_list_ok(list)) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    l = &s_daq[list];
    switch (mode) {
    case 0u:                        /* stop */
        l->state &= (uint8_t) ~XCP_DAQ_STATE_RUNNING;
        l->prescaler_cnt = 0u;
        break;
    case 1u:                        /* start */
        l->state |= XCP_DAQ_STATE_RUNNING;
        l->prescaler_cnt = 0u;
        break;
    case 2u:                        /* select（START_STOP_SYNCH 会启动所有选中列表） */
        l->state |= XCP_DAQ_STATE_SELECTED;
        break;
    default:
        send_err(XCP_ERR_MODE_NOT_VALID);
        return;
    }
    res[0] = l->first_pid;
    send_res(res, 1u);
}

static void cmd_start_stop_synch(const uint8_t *c, uint16_t len)
{
    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    switch (c[1]) {
    case 0u:                        /* stop all */
        for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
            s_daq[i].state &= (uint8_t) ~XCP_DAQ_STATE_RUNNING;
            s_daq[i].prescaler_cnt = 0u;
        }
        break;
    case 1u:                        /* start selected */
        for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
            if ((s_daq[i].state & XCP_DAQ_STATE_SELECTED) != 0u) {
                s_daq[i].state |= XCP_DAQ_STATE_RUNNING;
                s_daq[i].prescaler_cnt = 0u;
            }
        }
        break;
    case 2u:                        /* stop selected */
        for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
            if ((s_daq[i].state & XCP_DAQ_STATE_SELECTED) != 0u) {
                s_daq[i].state &= (uint8_t) ~XCP_DAQ_STATE_RUNNING;
            }
        }
        break;
    default:
        send_err(XCP_ERR_MODE_NOT_VALID);
        return;
    }
    send_ok();
}

static void cmd_get_daq_processor_info(void)
{
    uint8_t res[8];

    res[0] = (uint8_t) (XCP_DAQ_PROP_DYNAMIC | XCP_DAQ_PROP_PRESCALER | XCP_DAQ_PROP_TIMESTAMP);
    wr_u16(&res[1], XCP_MAX_DAQ_LISTS);         /* MAX_DAQ */
    wr_u16(&res[3], s_cfg->event_count);        /* MAX_EVENT_CHANNEL */
    res[5] = 0u;                                /* MIN_DAQ */
    res[6] = 0x00u;                             /* KEY_BYTE：IDF_ABS_ODT_NUMBER / AE 自由 / 默认优化 */
    send_res(res, 7u);
}

static void cmd_get_daq_resolution_info(void)
{
    uint8_t res[8];

    res[0] = 1u;                                /* GRANULARITY_ODT_ENTRY_SIZE_DAQ */
    res[1] = (uint8_t) (((XCP_MAX_DTO - 1u) > 255u) ? 255u : (XCP_MAX_DTO - 1u));  /* MAX_ODT_ENTRY_SIZE_DAQ */
    res[2] = 1u;                                /* GRANULARITY_ODT_ENTRY_SIZE_STIM */
    res[3] = 0u;                                /* MAX_ODT_ENTRY_SIZE_STIM（不支持 STIM） */
    res[4] = (uint8_t) ((XCP_TS_UNIT_1US << 4) | XCP_TS_SIZE_S4);  /* 可变长、1 us、S4 */
    wr_u16(&res[5], XCP_TS_TICKS);              /* TIMESTAMP_TICKS */
    send_res(res, 7u);
}

static void cmd_get_daq_event_info(const uint8_t *c, uint16_t len)
{
    uint8_t res[8];
    uint8_t evt;
    const xcp_event_def_t *e;

    if (len < 2u) {
        send_err(XCP_ERR_CMD_SYNTAX);
        return;
    }
    evt = c[1];
    if (evt >= s_cfg->event_count) {
        send_err(XCP_ERR_OUT_OF_RANGE);
        return;
    }
    e = &s_cfg->events[evt];
    /* DAQ_EVENT_PROPERTIES：bit7..6 consistency=DAQ(01)，bit2 daq 方向 */
    res[0] = 0x44u;
    res[1] = e->max_daq_list;
    res[2] = (uint8_t) strlen(e->name);
    res[3] = (uint8_t) e->cycle;
    res[4] = e->unit;
    res[5] = e->priority;
    send_res(res, 6u);
    s_mta = (uint32_t) (uintptr_t) e->name;     /* 名字由主机 UPLOAD 取回 */
}

static void cmd_get_daq_clock(void)
{
    uint8_t res[4];

    wr_u32(res, xcp_now_us());
    send_res(res, 4u);
}

/* ------------------------------------------------------------------ */
/* 分发                                                                */
/* ------------------------------------------------------------------ */

void xcp_rx_cto(const uint8_t *buf, uint16_t len)
{
    uint8_t cmd;

    if ((buf == NULL) || (len == 0u) || (len > (uint16_t) XCP_MAX_CTO)) {
        return;
    }
    s_stats.cto_rx++;
    cmd = buf[0];

    if (!s_connected && (cmd != XCP_CMD_CONNECT)) {
        s_stats.cmd_ignored++;
        return;
    }

    switch (cmd) {
    case XCP_CMD_CONNECT:                 cmd_connect(buf, len); break;
    case XCP_CMD_DISCONNECT:              cmd_disconnect(); break;
    case XCP_CMD_GET_STATUS:              cmd_get_status(); break;
    case XCP_CMD_SYNCH:                   /* 同步命令不响应 */ break;
    case XCP_CMD_GET_COMM_MODE_INFO:      cmd_get_comm_mode_info(); break;
    case XCP_CMD_GET_ID:                  cmd_get_id(buf, len); break;
    case XCP_CMD_SET_REQUEST:             cmd_set_request(buf, len); break;
    case XCP_CMD_GET_SEED:                cmd_get_seed(buf, len); break;
    case XCP_CMD_UNLOCK:                  cmd_unlock(buf, len); break;
    case XCP_CMD_SET_MTA:                 cmd_set_mta(buf, len); break;
    case XCP_CMD_UPLOAD:                  cmd_upload(buf, len); break;
    case XCP_CMD_SHORT_UPLOAD:            cmd_short_upload(buf, len); break;
    case XCP_CMD_DOWNLOAD:                cmd_download(buf, len); break;
    case XCP_CMD_MODIFY_BITS:             cmd_modify_bits(buf, len); break;
    case XCP_CMD_GET_PAG_PROCESSOR_INFO:  cmd_get_pag_processor_info(); break;
    case XCP_CMD_GET_SEGMENT_INFO:        cmd_get_segment_info(buf, len); break;
    case XCP_CMD_GET_PAGE_INFO:           cmd_get_page_info(buf, len); break;
    case XCP_CMD_SET_CAL_PAGE:            cmd_set_cal_page(buf, len); break;
    case XCP_CMD_GET_CAL_PAGE:            cmd_get_cal_page(buf, len); break;
    case XCP_CMD_FREE_DAQ:                cmd_free_daq(); break;
    case XCP_CMD_ALLOC_DAQ:               cmd_alloc_daq(buf, len); break;
    case XCP_CMD_ALLOC_ODT:               cmd_alloc_odt(buf, len); break;
    case XCP_CMD_ALLOC_ODT_ENTRY:         cmd_alloc_odt_entry(buf, len); break;
    case XCP_CMD_SET_DAQ_PTR:             cmd_set_daq_ptr(buf, len); break;
    case XCP_CMD_WRITE_DAQ:               cmd_write_daq(buf, len); break;
    case XCP_CMD_READ_DAQ:                cmd_read_daq(); break;
    case XCP_CMD_CLEAR_DAQ_LIST:          cmd_clear_daq_list(buf, len); break;
    case XCP_CMD_SET_DAQ_LIST_MODE:       cmd_set_daq_list_mode(buf, len); break;
    case XCP_CMD_GET_DAQ_LIST_MODE:       cmd_get_daq_list_mode(buf, len); break;
    case XCP_CMD_START_STOP_DAQ_LIST:     cmd_start_stop_daq_list(buf, len); break;
    case XCP_CMD_START_STOP_SYNCH:        cmd_start_stop_synch(buf, len); break;
    case XCP_CMD_GET_DAQ_PROCESSOR_INFO:  cmd_get_daq_processor_info(); break;
    case XCP_CMD_GET_DAQ_RESOLUTION_INFO: cmd_get_daq_resolution_info(); break;
    case XCP_CMD_GET_DAQ_EVENT_INFO:      cmd_get_daq_event_info(buf, len); break;
    case XCP_CMD_GET_DAQ_CLOCK:           cmd_get_daq_clock(); break;
    default:
        send_err(XCP_ERR_CMD_UNKNOWN);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* DAQ 采样（事件中断里调用）                                          */
/* ------------------------------------------------------------------ */

void xcp_event(uint8_t evt, uint32_t ts_us)
{
    if (!s_connected || s_tp == NULL) {
        return;
    }
    if (evt >= XCP_MAX_EVENTS) {
        return;
    }
    s_stats.daq_events[evt]++;

    for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
        xcp_daq_list_t *l = &s_daq[i];
        if ((l->in_use == 0u) || ((l->state & XCP_DAQ_STATE_RUNNING) == 0u) || (l->event != evt)) {
            continue;
        }
        if (l->prescaler > 1u) {
            l->prescaler_cnt++;
            if (l->prescaler_cnt < l->prescaler) {
                continue;
            }
            l->prescaler_cnt = 0u;
        }
        bool has_ts = ((l->mode & XCP_DAQ_MODE_TIMESTAMP) != 0u);
        for (uint8_t o = 0; o < l->odt_count; o++) {
            uint8_t buf[XCP_MAX_DTO];
            uint16_t pos = 0u;

            buf[pos++] = (uint8_t) (l->first_pid + o);      /* PID = 绝对 ODT 号 */
            if (has_ts && (o == 0u)) {
                wr_u32(&buf[pos], ts_us);
                pos = (uint16_t) (pos + 4u);
            }
            for (uint8_t e = 0; e < l->odt_entry_count[o]; e++) {
                if ((uint16_t) (pos + l->entry[o][e].size) > (uint16_t) XCP_MAX_DTO) {
                    break;
                }
                odt_pack_entry(&buf[pos], &l->entry[o][e]);
                pos = (uint16_t) (pos + l->entry[o][e].size);
            }
            (void) q_push(buf, pos);
        }
        s_stats.daq_samples++;
    }
}

/* ------------------------------------------------------------------ */
/* 主循环                                                              */
/* ------------------------------------------------------------------ */

void xcp_poll(void)
{
    /* 1) 先把事件中断攒下的 DAQ 帧发出去（延迟最小） */
    while (s_q_tail != s_q_head) {
        xcp_tx_frame_t *f = &s_q[s_q_tail];
        if (s_tp->send_dto(f->data, f->len) != 0) {
            s_stats.daq_overload++;
            /* 首次发生过载时按规范发一个 EVENT 包（EVC_DAQ_OVERLOAD = 0x06），避免主机误以为没丢 */
            if (!s_ovl_reported) {
                send_ev(0x06u, NULL, 0u);
                s_ovl_reported = true;
            }
            break;                  /* CAN TX FIFO 满：下一轮再试 */
        }
        s_q_tail = (uint16_t) ((s_q_tail + 1u) % XCP_DAQ_TX_QUEUE_LEN);
        s_stats.daq_frames++;
        s_ovl_reported = false;
    }
    /* 2) 收 CTO */
    if (s_tp->poll != NULL) {
        s_tp->poll();
    }
}

/* ------------------------------------------------------------------ */
/* 初始化与查询                                                        */
/* ------------------------------------------------------------------ */

void xcp_set_transport(const xcp_transport_ops_t *ops)
{
    s_tp = ops;
}

int xcp_init(const xcp_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->regions == NULL) || (cfg->now_us == NULL) || (s_tp == NULL)) {
        return -1;
    }
    if (cfg->event_count > XCP_MAX_EVENTS) {
        return -1;
    }
    s_cfg = cfg;
    s_connected = false;
    s_mta = 0u;
    s_mta_ext = 0u;
    s_last_error = 0u;
    s_state_number = 0u;
    s_session_cfg = 0u;
    s_pag_segment = 0u;
    s_pag_page = 0u;
    s_protection = 0u;
    s_unlocked = true;
    s_seed_len = (cfg->seed_len > sizeof(s_seed)) ? (uint8_t) sizeof(s_seed) : cfg->seed_len;
    if (cfg->seed != NULL) {
        memcpy(s_seed, cfg->seed, s_seed_len);
    } else {
        s_seed_len = 0u;
    }
    s_q_head = 0u;
    s_q_tail = 0u;
    daq_clear_all();
    xcp_reset_stats();
    return 0;
}

void xcp_set_seed(const uint8_t *seed, uint8_t len)
{
    if (len > sizeof(s_seed)) {
        len = (uint8_t) sizeof(s_seed);
    }
    if ((seed == NULL) || (len == 0u)) {
        s_seed_len = 0u;
        s_protection = 0u;
        s_unlocked = true;
        return;
    }
    memcpy(s_seed, seed, len);
    s_seed_len = len;
    s_protection = (uint8_t) (XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG);
    s_unlocked = false;
}

void xcp_get_stats(xcp_stats_t *st)
{
    if (st != NULL) {
        *st = s_stats;
    }
}

void xcp_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

bool xcp_is_connected(void)
{
    return s_connected;
}

uint8_t xcp_last_error(void)
{
    return s_last_error;
}

uint8_t xcp_daq_list_count(void)
{
    return s_daq_allocated;
}

int xcp_get_daq_list_info(uint8_t idx, xcp_daq_list_info_t *out)
{
    if ((out == NULL) || (idx >= XCP_MAX_DAQ_LISTS) || (s_daq[idx].in_use == 0u)) {
        return -1;
    }
    out->in_use = s_daq[idx].in_use;
    out->state = s_daq[idx].state;
    out->mode = s_daq[idx].mode;
    out->event = s_daq[idx].event;
    out->prescaler = s_daq[idx].prescaler;
    out->prescaler_cnt = s_daq[idx].prescaler_cnt;
    out->priority = s_daq[idx].priority;
    out->first_pid = s_daq[idx].first_pid;
    out->odt_count = s_daq[idx].odt_count;
    memcpy(out->odt_entry_count, s_daq[idx].odt_entry_count, sizeof(out->odt_entry_count));
    return 0;
}
