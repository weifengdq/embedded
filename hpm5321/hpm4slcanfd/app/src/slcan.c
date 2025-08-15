#include "slcan.h"

#include <string.h>

#include "cdc_acm.h"
#include "mcan.h"

// clang-format off
slcan_instance_t slcan[SLCAN_NUM] = {
    { SLCAN0, false, 500000, 2000000},
    { SLCAN1, false, 500000, 2000000},
    { SLCAN2, false, 500000, 2000000},
    { SLCAN3, false, 500000, 2000000}
};
// clang-format on

slcan_instance_t *slcan_get_instance(slcan_channel_t channel)
{
    if (channel < SLCAN_NUM) {
        return &slcan[channel];
    }
    return NULL; // Invalid channel
}

int slcan_can_set_nbaud(slcan_channel_t channel, slcan_nbaud_t can_nbaud_index)
{
    static const uint32_t baud_table[] = {
        10000,  // SLCAN_BAUD_10K
        20000,  // SLCAN_BAUD_20K
        50000,  // SLCAN_BAUD_50K
        100000, // SLCAN_BAUD_100K
        125000, // SLCAN_BAUD_125K
        250000, // SLCAN_BAUD_250K
        500000, // SLCAN_BAUD_500K
        800000, // SLCAN_BAUD_800K
        1000000 // SLCAN_BAUD_1000K
    };

    slcan_instance_t *instance = slcan_get_instance(channel);
    if (!instance) {
        return -1; // invalid channel
    }
    if (can_nbaud_index < SLCAN_BAUD_INVALID) {
        instance->nbaud = baud_table[can_nbaud_index];
        mcan_set_nbaud((mcan_channel_t) channel, instance->nbaud);
        SLCAN_DEBUG("SLCAN%d nbaud set to %d\n", (int) channel, instance->nbaud);
        return 0;
    }
    return -2; // invalid baud index
}

int slcan_can_set_dbaud(slcan_channel_t channel, slcan_dbaud_t can_dbaud_index)
{
    static const uint32_t dbaud_table[] = {
        0,        // SLCAN_DBAUD_0M (not used)
        1000000,  // SLCAN_DBAUD_1M
        2000000,  // SLCAN_DBAUD_2M
        3000000,  // SLCAN_DBAUD_3M
        4000000,  // SLCAN_DBAUD_4M
        5000000,  // SLCAN_DBAUD_5M
        6000000,  // SLCAN_DBAUD_6M
        7000000,  // SLCAN_DBAUD_7M (Placeholder)
        8000000,  // SLCAN_DBAUD_8M
        9000000,  // SLCAN_DBAUD_9M (Placeholder)
        10000000, // SLCAN_DBAUD_10M
        11000000, // SLCAN_DBAUD_11M (Placeholder)
        12000000, // SLCAN_DBAUD_12M
        13000000, // SLCAN_DBAUD_13M (Placeholder)
        14000000, // SLCAN_DBAUD_14M (Placeholder)
        15000000  // SLCAN_DBAUD_15M
    };

    slcan_instance_t *instance = slcan_get_instance(channel);
    if (!instance) {
        return -1; // invalid channel
    }
    if (can_dbaud_index > SLCAN_DBAUD_0M && can_dbaud_index < SLCAN_DBAUD_INVALID) {
        instance->dbaud = dbaud_table[can_dbaud_index];
        mcan_set_dbaud((mcan_channel_t) channel, instance->dbaud);
        SLCAN_DEBUG("SLCAN%d dbaud set to %d\n", (int) channel, instance->dbaud);
        return 0;
    }
    return -2; // invalid dbaud index
}

int slcan_can_open(slcan_channel_t channel)
{
    slcan_instance_t *instance = slcan_get_instance(channel);
    if (!instance) {
        return -1;
    }

    if (mcan_open((mcan_channel_t) channel) == 0) {
        instance->is_opened = true;
        SLCAN_DEBUG("SLCAN%d opened with nbaud: %d, dbaud: %d\n",
                    (int) channel,
                    instance->nbaud,
                    instance->dbaud);
    }

    return 0;
}

int slcan_can_close(slcan_channel_t channel)
{
    slcan_instance_t *instance = slcan_get_instance(channel);
    if (!instance) {
        return -1;
    }

    if (mcan_close((mcan_channel_t) channel)) {
        instance->is_opened = false;
        SLCAN_DEBUG("SLCAN%d closed\n", (int) channel);
    }

    return 0;
}

/* write a byte as two uppercase hex chars into buffer p
 * returns number of chars written (2) or -1 on insufficient space
 */
static inline int write_hex_byte_to_buf(char *p, int remain, uint8_t val)
{
    const char hexmap[] = "0123456789ABCDEF";
    if (remain < 2) {
        return -1;
    }
    p[0] = hexmap[(val >> 4) & 0xF];
    p[1] = hexmap[val & 0xF];
    return 2;
}

static uint8_t asc2nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return (c - '0');
    } else if (c >= 'A' && c <= 'F') {
        return (c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
        return (c - 'a' + 10);
    }
    return 16; // Invalid nibble
}

/**
 * Convert a CAN-FD frame to SLCAN ASCII string.
 * - out must have enough space (recommend >= SLCAN_MTU)
 * - returns number of bytes written to out (excluding NUL), or -1 on error
 */
int canfd_frame2slcan(slcan_channel_t channel,
                      const struct canfd_frame *frame,
                      char *out,
                      int out_len)
{
    if (!frame || !out || out_len <= 0) {
        return -1;
    }

    /* reverse DLC mapping used in process_uart */
    // static const uint8_t len2dlc[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    // const uint8_t dlc_table_len = sizeof(len2dlc) / sizeof(len2dlc[0]);
    uint8_t dlc = 0xFF;
    /* find dlc index by matching length to mapping used previously
     * mapping in process_uart: index->len: {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64}
     */
    static const uint8_t dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (uint8_t i = 0; i < 16; i++) {
        if (frame->len == dlc2len[i]) {
            dlc = i;
            break;
        }
    }
    if (dlc == 0xFF) {
        /* unsupported length */
        return -1;
    }

    bool is_ext = (frame->can_id & CAN_EFF_FLAG) != 0;
    bool is_rtr = (frame->can_id & CAN_RTR_FLAG) != 0;
    bool is_fd = (frame->flags & CANFD_FDF) != 0;
    bool is_brs = (frame->flags & CANFD_BRS) != 0;

    char *p = out;
    int remain = out_len;

    /* select command letter */
    char cmd = 0;
    if (is_rtr) {
        cmd = is_ext ? 'R' : 'r';
    } else if (is_fd) {
        if (is_ext) {
            cmd = is_brs ? 'B' : 'D';
        } else {
            cmd = is_brs ? 'b' : 'd';
        }
    } else {
        cmd = is_ext ? 'T' : 't';
    }

    /* write cmd */
    if (remain < 2) { /* at least cmd + '\r' */
        return -1;
    }
    *p++ = cmd;
    remain--;

    /* write id */
    uint32_t id = frame->can_id & (is_ext ? 0x1FFFFFFFUL : 0x7FFU);
    if (is_ext) {
        /* 8 hex chars */
        int needed = 8;
        if (remain <= needed)
            return -1;
        int n = snprintf(p, remain, "%08X", id);
        if (n <= 0 || n >= remain)
            return -1;
        p += n;
        remain -= n;
    } else {
        /* 3 hex chars */
        int needed = 3;
        if (remain <= needed)
            return -1;
        int n = snprintf(p, remain, "%03X", id & 0x7FFU);
        if (n <= 0 || n >= remain)
            return -1;
        p += n;
        remain -= n;
    }

    /* write dlc nibble as single hex char */
    if (remain < 2)
        return -1;
    const char hexmap[] = "0123456789ABCDEF";
    *p++ = hexmap[dlc & 0xF];
    remain--;

    /* remote frames have no data bytes */
    if (!is_rtr) {
        /* write data bytes as hex */
        for (uint8_t i = 0; i < frame->len; i++) {
            if (remain < 3) {
                return -1; /* two hex + maybe null later */
            }
            int n = write_hex_byte_to_buf(p, remain, frame->data[i]);
            if (n != 2) {
                return -1;
            }
            p += 2;
            remain -= 2;
        }
    }

    /* terminate with CR */
    if (remain < 1)
        return -1;
    *p++ = '\r';
    remain--;

    return (int) (p - out);
}


void process_uart(slcan_channel_t channel, const char *data, int len)
{
    int ack_len = 0;
    char ack[10];
    char cmd = data[0];
    slcan_instance_t *instance = slcan_get_instance(channel);
    switch (cmd) {
    case 'r': // 标准远程帧, riiil
    case 't': // 标准数据帧, tiiilxxxx...
    case 'd': // 标准CANFD, // diiilxxxx...
    case 'b': // 标准CANFD+BRS, // biiilxxxx...
    case 'R': // 扩展远程帧, Riiiiiiil
    case 'T': // 扩展数据帧, Tiiiiiiilxxxx...
    case 'D': // 扩展CANFD, // Diiiiiiilxxxx...
    case 'B': // 扩展CANFD+BRS, // Biiiiiiilxxxx...
    {
        // print cmd_str
        char str[SLCAN_MTU + 1] = {0};
        memcpy(str, data, len);
        str[len] = '\0'; // Null-terminate the string for printing
        SLCAN_DEBUG("Received: %s\n", str);
        if (len < 5) {
            SLCAN_DEBUG("Invalid frame length: %d\n", len);
            return;
        }
        if (instance && instance->is_opened) {
            struct canfd_frame frame;
            memset(&frame, 0, sizeof(frame));
            frame.flags = 0;

            // clang-format off
            if (cmd == 'r') { frame.can_id |= CAN_RTR_FLAG; }
            else if (cmd == 'd') { frame.flags |= CANFD_FDF; }
            else if (cmd == 'b') { frame.flags |= CANFD_FDF | CANFD_BRS; }
            else if (cmd == 'R') { frame.can_id |= CAN_EFF_FLAG | CAN_RTR_FLAG; }
            else if (cmd == 'T') { frame.can_id |= CAN_EFF_FLAG; }
            else if (cmd == 'D') { frame.can_id |= CAN_EFF_FLAG; frame.flags |= CANFD_FDF; }
            else if (cmd == 'B') { frame.can_id |= CAN_EFF_FLAG; frame.flags |= CANFD_FDF | CANFD_BRS; }
            // clang-format on

            static const uint8_t dlc2len[] = {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
            // 解析 can_id
            int index = 1;
            uint32_t id = 0;
            uint32_t id_len = 3;
            if ((frame.can_id & CAN_EFF_FLAG) && len > 9) {
                id_len = 8;
            }
            for (int i = index; i < index + id_len; i++) {
                id <<= 4;
                id |= asc2nibble(data[i]);
            }
            frame.can_id |= id;
            index += (int)id_len;

            // 解析 len
            uint8_t dlc = asc2nibble(data[index]);
            if (dlc > CANFD_MAX_DLC) {
                SLCAN_DEBUG("Invalid DLC: %d\n", dlc);
                return;
            }
            frame.len = dlc2len[dlc];
            index += 1;

            // 远程帧无数据
            if (!(frame.can_id & CAN_RTR_FLAG)) {
                // 解析十六进制数据
                if (len < index + frame.len * 2) {
                    SLCAN_DEBUG("Invalid frame length: %d\n", len);
                    return;
                }
                for (int i = index; i < index + frame.len * 2; i += 2) {
                    uint8_t byte = (asc2nibble(data[i]) << 4) | asc2nibble(data[i + 1]);
                    frame.data[i / 2 - index / 2] = byte;
                }
            }

            {
                SLCAN_DEBUG("SLCAN%d Sent frame: %c, ID: 0x%08X, DLC: %d, flags: 0x%02X, Data: ",
                            (int) channel,
                            cmd,
                            frame.can_id,
                            frame.len,
                            frame.flags);
                for (int i = 0; i < frame.len; i++) {
                    SLCAN_DEBUG("%02X ", frame.data[i]);
                }
                SLCAN_DEBUG("\n");
            }

            // 发送 CAN 帧
            if (mcan_send((mcan_channel_t) channel, &frame) < 0) {
                SLCAN_DEBUG("Failed to send CAN frame on channel %d\n", (int) channel);
            }
        } else {
            SLCAN_DEBUG("SLCAN%d is not opened, cannot send frame\n", (int) channel);
        }
    } break;

    case 'S': // Set nbaud
    {
        ack_len = snprintf(ack, sizeof(ack), "\r");
        if (len > 1) {
            slcan_nbaud_t nbaud_index = (slcan_nbaud_t) (data[1] - '0');
            slcan_can_set_nbaud(channel, nbaud_index);
        }
        SLCAN_DEBUG("SLCAN%d nbaud set to %d\n", (int) channel, instance->nbaud);
    } break;
    case 'Y': // Set dbaud
    {
        ack_len = snprintf(ack, sizeof(ack), "\r");
        if (len > 1) {
            slcan_dbaud_t dbaud_index = (slcan_dbaud_t) (data[1] - '0');
            slcan_can_set_dbaud(channel, dbaud_index);
        }
        SLCAN_DEBUG("SLCAN%d dbaud set to %d\n", (int) channel, instance->dbaud);
    } break;
    case 'O': // Open CAN
    {
        ack_len = snprintf(ack, sizeof(ack), "\r");
        if (instance && !instance->is_opened) {
            slcan_can_open(channel);
        } else {
            slcan_can_close(channel); // Ensure CAN is closed before opening
            slcan_can_open(channel);
        }
        SLCAN_DEBUG("SLCAN%d opened\n", (int) channel);
    } break;
    case 'C': // Close CAN
    {
        ack_len = snprintf(ack, sizeof(ack), "\r");
        slcan_can_close(channel);
        SLCAN_DEBUG("SLCAN%d closed\n", (int) channel);
    } break;
    case 'V': // Version (Software)
    {
        ack_len = snprintf(ack, sizeof(ack), "V2508\r");
        SLCAN_DEBUG("SLCAN%d Software Version: %s\n", (int) channel, ack);
    } break;
    case 'v': // Version (Hardware)
    {
        ack_len = snprintf(ack, sizeof(ack), "v1.0\r");
        SLCAN_DEBUG("SLCAN%d Hardware Version: %s\n", (int) channel, ack);
    } break;
    case 'F': // Status Flag
    {
        ack_len = snprintf(ack, sizeof(ack), "F00\r");
        SLCAN_DEBUG("SLCAN%d Status Flag: %s\n", (int) channel, ack);
    } break;
    default:
        SLCAN_DEBUG("Unknown command: %c\n", cmd);
        break;
    }

    if (ack_len > 0) {
        // ack[ack_len] = '\0'; // Null-terminate the string
        slcan_uart_write(channel, ack, ack_len);
        SLCAN_DEBUG("SLCAN%d Response: %s\n", (int) channel, ack);
    }
}

void slcan_process_task(slcan_instance_t *instance)
{
    int num = 1;
    while (num) {
        num = slcan_uart_read(instance->channel);
        if (num < 0) {
            memset(instance->serial_rx_buffer, 0, sizeof(instance->serial_rx_buffer));
            return;
        }
        process_uart(instance->channel, instance->serial_rx_buffer, instance->serial_rx_length);
        instance->serial_rx_length = 0; // Reset after processing
        memset(instance->serial_rx_buffer, 0, sizeof(instance->serial_rx_buffer));
    }
}