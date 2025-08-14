#ifndef SLCAN_H
#define SLCAN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "linux_can.h"

// #define SLCAN_USE_DEBUG

#ifndef SLCAN_USE_DEBUG
#define SLCAN_DEBUG(...)
#else
#define SLCAN_DEBUG(...) printf(__VA_ARGS__)
#endif

#define SLCAN_MTU (sizeof("B12345678FEA5F\r") + 1 + 64 * 2) // SLCANFD

typedef enum slcan_channel { SLCAN0 = 0, SLCAN1, SLCAN2, SLCAN3, SLCAN_NUM } slcan_channel_t;

typedef enum slcan_uart_status {
    SLCAN_UART_EXIT = 0,
    SLCAN_UART_ACK,
    SLCAN_UART_NACK,
    SLCAN_UART_REPLY,
    SLCAN_UART_SEND,
} slcan_uart_status_t;

typedef enum slcan_nbaud {
    SLCAN_BAUD_10K = 0,
    SLCAN_BAUD_20K,
    SLCAN_BAUD_50K,
    SLCAN_BAUD_100K,
    SLCAN_BAUD_125K,
    SLCAN_BAUD_250K,
    SLCAN_BAUD_500K, // S6
    SLCAN_BAUD_800K,
    SLCAN_BAUD_1000K, // S8
    SLCAN_BAUD_INVALID,
} slcan_nbaud_t;

typedef enum slcan_dbaud {
    SLCAN_DBAUD_0M = 0, // 0M is not used
    SLCAN_DBAUD_1M = 1,
    SLCAN_DBAUD_2M,
    SLCAN_DBAUD_3M,
    SLCAN_DBAUD_4M,
    SLCAN_DBAUD_5M,
    SLCAN_DBAUD_6M,
    SLCAN_DBAUD_7M, // Placeholder
    SLCAN_DBAUD_8M,
    SLCAN_DBAUD_9M,  // Placeholder
    SLCAN_DBAUD_10M, // A
    SLCAN_DBAUD_11M, // B, Placeholder
    SLCAN_DBAUD_12M, // C
    SLCAN_DBAUD_13M, // D, Placeholder
    SLCAN_DBAUD_14M, // E, Placeholder
    SLCAN_DBAUD_15M, // F
    SLCAN_DBAUD_INVALID,
} slcan_dbaud_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct slcan_instance {
    slcan_channel_t channel;
    bool is_opened;
    uint32_t nbaud;
    uint32_t dbaud;
    int serial_rx_length;
    char serial_rx_buffer[SLCAN_MTU];
} slcan_instance_t;

extern slcan_instance_t slcan[SLCAN_NUM];

slcan_instance_t *slcan_get_instance(slcan_channel_t channel);

int slcan_can_set_nbaud(slcan_channel_t channel, slcan_nbaud_t can_nbaud_index);
int slcan_can_set_dbaud(slcan_channel_t channel, slcan_dbaud_t can_dbaud_index);
int slcan_can_open(slcan_channel_t channel);
int slcan_can_close(slcan_channel_t channel);

void process_uart(slcan_channel_t channel, const char *data, int len);
// void process_can(slcan_channel_t channel, const struct canfd_frame *frame);

void slcan_process_task(slcan_instance_t *instance);

#ifdef __cplusplus
}
#endif

#endif /* SLCAN_H */
