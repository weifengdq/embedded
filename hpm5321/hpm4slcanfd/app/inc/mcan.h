#ifndef MCAN_H
#define MCAN_H

#include <stdint.h>
#include <stdio.h>

#include "linux_can.h"

#define MCAN_USE_DEBUG

typedef enum { M_CAN0 = 0, M_CAN1, M_CAN2, M_CAN3, M_CAN_NUM } mcan_channel_t;

#ifndef MCAN_USE_DEBUG
#define MCAN_DEBUG(...)
#else
#define MCAN_DEBUG(...) printf(__VA_ARGS__)
#endif

int mcan_set_nbaud(mcan_channel_t channel, uint32_t nbaud);
int mcan_set_dbaud(mcan_channel_t channel, uint32_t dbaud);
int mcan_open(mcan_channel_t channel);
int mcan_close(mcan_channel_t channel);

int mcan_send(mcan_channel_t channel, const struct canfd_frame *frame);

#endif
