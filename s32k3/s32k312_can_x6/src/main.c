#include "Clock_Ip.h"
#include "FlexCAN_Ip.h"
#include "FlexCAN_Ip_HwAccess.h"
#include "IntCtrl_Ip.h"
#include "Mcal.h"
#include "Siul2_Port_Ip.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CAN_EFF_FLAG 0x80000000U
#define CAN_RTR_FLAG 0x40000000U
#define CAN_EFF_MASK 0x1FFFFFFFU
#define CAN_SFF_MASK 0x000007FFU
#define CANFD_BRS 0x01
#define CANFD_FDF 0x04
#define CAN_CIRCLE_BUF_SIZE 32
#define CAN_CHANNEL_NUM 6

typedef uint32_t canid_t;
struct canfd_frame {
  canid_t can_id;
  uint8_t len;
  uint8_t flags;
  uint8_t __res0;
  uint8_t __res1;
  uint8_t data[64];
};

struct canfd_frame_circlebuf {
  struct canfd_frame buf[CAN_CIRCLE_BUF_SIZE];
  volatile uint8_t head;
  volatile uint8_t tail;
  volatile uint8_t ready;
};

static inline int canfd_cbuf_is_empty(const struct canfd_frame_circlebuf *cbuf) {
  return cbuf->head == cbuf->tail;
}
static inline int canfd_cbuf_is_full(const struct canfd_frame_circlebuf *cbuf) {
  return ((cbuf->head + 1) % CAN_CIRCLE_BUF_SIZE) == cbuf->tail;
}
static inline int canfd_cbuf_push(struct canfd_frame_circlebuf *cbuf, const struct canfd_frame *frame) {
  if (canfd_cbuf_is_full(cbuf)) return -1;
  memcpy(&cbuf->buf[cbuf->head], frame, sizeof(struct canfd_frame));
  cbuf->head = (cbuf->head + 1) % CAN_CIRCLE_BUF_SIZE;
  cbuf->ready = 1;
  return 0;
}
static inline int canfd_cbuf_pop(struct canfd_frame_circlebuf *cbuf, struct canfd_frame *frame) {
  if (canfd_cbuf_is_empty(cbuf)) { cbuf->ready = 0; return -1; }
  memcpy(frame, &cbuf->buf[cbuf->tail], sizeof(struct canfd_frame));
  cbuf->tail = (cbuf->tail + 1) % CAN_CIRCLE_BUF_SIZE;
  if (cbuf->tail == cbuf->head) cbuf->ready = 0;
  return 0;
}

// 6路实例相关定义
const uint8_t FLEXCAN_INSTANCE[CAN_CHANNEL_NUM] = {0,1,2,3,4,5};
const uint8_t RX_STD_MB_IDX = 0;
const uint8_t RX_EXT_MB_IDX = 1;
static Flexcan_Ip_MsgBuffType can_rx_msg[CAN_CHANNEL_NUM][2];
static volatile uint8_t can_tx_busy[CAN_CHANNEL_NUM] = {0};
// 未初始化 .bss, 初始化 .data
__attribute__((section(".dtcm_bss"))) struct canfd_frame_circlebuf can_rx_cbuf[CAN_CHANNEL_NUM];
__attribute__((section(".dtcm_bss"))) struct canfd_frame_circlebuf can_tx_cbuf[CAN_CHANNEL_NUM];

static Flexcan_Ip_DataInfoType rx_std_info[CAN_CHANNEL_NUM];
static Flexcan_Ip_DataInfoType rx_ext_info[CAN_CHANNEL_NUM];

// FlexCAN消息与canfd_frame转换
static inline void flexcan_msg_to_canfd_frame(const Flexcan_Ip_MsgBuffType *msg, struct canfd_frame *frame) {
  bool is_ext = !!(msg->cs & FLEXCAN_IP_CS_IDE_MASK);
  bool is_fdf = !!(msg->cs & FLEXCAN_IP_MB_EDL_MASK);
  bool is_brs = !!(msg->cs & FLEXCAN_IP_MB_BRS_MASK);
  bool is_rmt = !!(msg->cs & FLEXCAN_IP_CS_RTR_MASK);
  uint8_t dlc = (msg->cs & FLEXCAN_IP_CS_DLC_MASK) >> FLEXCAN_IP_CS_DLC_SHIFT;
  const uint8_t dlc2len[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
  frame->can_id = msg->msgId | (is_ext ? CAN_EFF_FLAG : 0) | (is_rmt ? CAN_RTR_FLAG : 0);
  frame->len = dlc2len[dlc];
  frame->flags = (is_fdf ? CANFD_FDF : 0) | (is_brs ? CANFD_BRS : 0);
  memcpy(frame->data, msg->data, frame->len);
}

static inline void canfd_frame_to_tx(const struct canfd_frame *frame, Flexcan_Ip_DataInfoType *tx_info, uint32_t *msg_id, uint8_t *data) {
  tx_info->msg_id_type = (frame->can_id & CAN_EFF_FLAG) ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
  tx_info->data_length = frame->len;
  tx_info->is_remote = (frame->can_id & CAN_RTR_FLAG) ? true : false;
  tx_info->is_polling = false;
  tx_info->enable_brs = (frame->flags & CANFD_BRS) ? true : false;
  tx_info->fd_enable = (tx_info->enable_brs || (frame->flags & CANFD_FDF)) ? true : false;
  tx_info->fd_padding = 0;
  *msg_id = frame->can_id & CAN_EFF_FLAG ? (frame->can_id & CAN_EFF_MASK) : (frame->can_id & CAN_SFF_MASK);
  memcpy(data, frame->data, frame->len);
}

// 回调函数
void FlexCAN_UserCallback(uint8 instance, Flexcan_Ip_EventType eventType, uint32 buffIdx, const Flexcan_Ip_StateType *flexcanState) {
  (void)flexcanState;
  if (FLEXCAN_EVENT_RX_COMPLETE == eventType) {
    if (buffIdx >= 2) return;
    struct canfd_frame frame;
    flexcan_msg_to_canfd_frame(&can_rx_msg[instance][buffIdx], &frame);
    canfd_cbuf_push(&can_rx_cbuf[instance], &frame);
    FlexCAN_Ip_Receive(instance, buffIdx, &can_rx_msg[instance][buffIdx], false);
  } else if (FLEXCAN_EVENT_TX_COMPLETE == eventType) {
    struct canfd_frame frame;
    if (canfd_cbuf_pop(&can_tx_cbuf[instance], &frame) == 0) {
      Flexcan_Ip_DataInfoType tx_info;
      uint32_t msg_id;
      uint8_t data[64];
      canfd_frame_to_tx(&frame, &tx_info, &msg_id, data);
      int ret = FlexCAN_Ip_Send(instance, 2, &tx_info, msg_id, data);
      if (ret == FLEXCAN_STATUS_SUCCESS) {
        can_tx_busy[instance] = 1;
      }
    } else {
      can_tx_busy[instance] = 0;
    }
  }
}

void FlexCAN_ErrorCallback(uint8 instance, Flexcan_Ip_EventType eventType, uint32 u32ErrStatus, const Flexcan_Ip_StateType *flexcanState) {
  (void)instance; (void)eventType; (void)u32ErrStatus; (void)flexcanState;
}

int bsp_can_send(uint8 channel, struct canfd_frame *frame) {
  FlexCAN_Ip_DisableInterrupts(channel);
  if (!can_tx_busy[channel]) {
    Flexcan_Ip_DataInfoType tx_info;
    uint32_t msg_id;
    uint8_t data[64];
    canfd_frame_to_tx(frame, &tx_info, &msg_id, data);
    int ret = FlexCAN_Ip_Send(channel, 2, &tx_info, msg_id, data);
    if (ret == FLEXCAN_STATUS_SUCCESS) {
      can_tx_busy[channel] = 1;
      FlexCAN_Ip_EnableInterrupts(channel);
      return 0;
    } else if (ret == FLEXCAN_STATUS_BUFF_OUT_OF_RANGE) {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -1;
    } else if (ret == FLEXCAN_STATUS_BUSY) {
      if (canfd_cbuf_push(&can_tx_cbuf[channel], frame) != 0) {
        FlexCAN_Ip_EnableInterrupts(channel);
        return -4;
      }
      FlexCAN_Ip_EnableInterrupts(channel);
      return 1;
    } else {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -3;
    }
  } else {
    if (canfd_cbuf_push(&can_tx_cbuf[channel], frame) != 0) {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -4;
    }
    FlexCAN_Ip_EnableInterrupts(channel);
    return 0;
  }
}

void delay(int count) {
  int cnt = count * 1200;
  while (cnt--) {
    __asm__ volatile("nop");
  }
}

int main(void) {
  Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
  Siul2_Port_Ip_Init(NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
                     g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);
  IntCtrl_Ip_Init(&IntCtrlConfig_0);

  // 初始化6路FlexCAN
  FlexCAN_Ip_Init(0, &FlexCAN_State0, &FlexCAN_Config0);
  FlexCAN_Ip_Init(1, &FlexCAN_State1, &FlexCAN_Config1);
  FlexCAN_Ip_Init(2, &FlexCAN_State2, &FlexCAN_Config2);
  FlexCAN_Ip_Init(3, &FlexCAN_State3, &FlexCAN_Config3);
  FlexCAN_Ip_Init(4, &FlexCAN_State4, &FlexCAN_Config4);
  FlexCAN_Ip_Init(5, &FlexCAN_State5, &FlexCAN_Config5);
  for (uint8_t ch = 0; ch < CAN_CHANNEL_NUM; ch++) {
    FlexCAN_Ip_SetTDCOffset(ch, TRUE, 9U);
    FlexCAN_Ip_SetRxMaskType(ch, FLEXCAN_RX_MASK_INDIVIDUAL);
    FlexCAN_Ip_SetRxIndividualMask(ch, RX_STD_MB_IDX, 0x0U);
    rx_std_info[ch].msg_id_type = FLEXCAN_MSG_ID_STD;
    rx_std_info[ch].data_length = 64U;
    rx_std_info[ch].is_polling = false;
    rx_std_info[ch].is_remote = true;
    FlexCAN_Ip_ConfigRxMb(ch, RX_STD_MB_IDX, &rx_std_info[ch], 0x0U);
    FlexCAN_Ip_SetRxIndividualMask(ch, RX_EXT_MB_IDX, 0x0U);
    rx_ext_info[ch].msg_id_type = FLEXCAN_MSG_ID_EXT;
    rx_ext_info[ch].data_length = 64U;
    rx_ext_info[ch].is_polling = false;
    rx_ext_info[ch].is_remote = true;
    FlexCAN_Ip_ConfigRxMb(ch, RX_EXT_MB_IDX, &rx_ext_info[ch], 0x0U);
    FlexCAN_Ip_SetStartMode(ch);
    can_rx_cbuf[ch].head = can_rx_cbuf[ch].tail = can_rx_cbuf[ch].ready = 0;
    can_tx_cbuf[ch].head = can_tx_cbuf[ch].tail = can_tx_cbuf[ch].ready = 0;
    can_tx_busy[ch] = 0;
    FlexCAN_Ip_Receive(ch, RX_STD_MB_IDX, &can_rx_msg[ch][0], false);
    FlexCAN_Ip_Receive(ch, RX_EXT_MB_IDX, &can_rx_msg[ch][1], false);
  }

  while (1) {
    for (uint8_t ch = 0; ch < CAN_CHANNEL_NUM; ch++) {
      struct canfd_frame frame;
      while (canfd_cbuf_pop(&can_rx_cbuf[ch], &frame) == 0) {
        while (bsp_can_send(ch, &frame) != 0) {
          delay(1);
        }
      }
    }
  }
  return 0;
}
