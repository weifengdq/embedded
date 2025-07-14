#include "Clock_Ip.h"
#include "FlexCAN_Ip.h"
#include "FlexCAN_Ip_HwAccess.h"
#include "IntCtrl_Ip.h"
#include "Mcal.h"
#include "Siul2_Port_Ip.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Linux SocketCAN 风格
#define CAN_EFF_FLAG 0x80000000U
#define CAN_RTR_FLAG 0x40000000U
#define CAN_ERR_FLAG 0x20000000U
#define CAN_SFF_MASK 0x000007FFU
#define CAN_EFF_MASK 0x1FFFFFFFU
#define CAN_ERR_MASK 0x1FFFFFFFU
#define CANFD_BRS 0x01
#define CANFD_ESI 0x02
#define CANFD_FDF 0x04
typedef uint32_t canid_t;
struct canfd_frame {
  canid_t can_id; /* 32 bit CAN_ID + EFF/RTR/ERR flags */
  uint8_t len;    /* frame payload length in byte */
  uint8_t flags;  /* additional flags for CAN FD */
  uint8_t __res0; /* reserved / padding */
  uint8_t __res1; /* reserved / padding */
  uint8_t data[64];
};

const uint8_t FLEXCAN_INSTANCE = 0; // FlexCAN0
const uint8_t RX_STD_MB_IDX = 0;    // 接收标准帧的消息缓冲区索引
const uint8_t RX_EXT_MB_IDX = 1;    // 接收扩展帧
const uint8_t TX_MBS[5] = {2, 3, 4, 5, 6}; // 发送消息缓冲区索引

static Flexcan_Ip_DataInfoType rx_std_info = {.msg_id_type = FLEXCAN_MSG_ID_STD,
                                              .data_length = 64U,
                                              .is_polling = false,
                                              .is_remote = true};
static Flexcan_Ip_DataInfoType rx_ext_info = {.msg_id_type = FLEXCAN_MSG_ID_EXT,
                                              .data_length = 64U,
                                              .is_polling = false,
                                              .is_remote = true};
static Flexcan_Ip_MsgBuffType can_rx_msg[2];
static volatile uint8_t can_tx_busy = 0;

// 通用环形缓冲区结构体及操作函数
#define CAN_CIRCLE_BUF_SIZE 32
struct canfd_frame_circlebuf {
  struct canfd_frame buf[CAN_CIRCLE_BUF_SIZE];
  volatile uint8_t head;
  volatile uint8_t tail;
  volatile uint8_t ready;
};

static inline int
canfd_cbuf_is_empty(const struct canfd_frame_circlebuf *cbuf) {
  return cbuf->head == cbuf->tail;
}

static inline int canfd_cbuf_is_full(const struct canfd_frame_circlebuf *cbuf) {
  return ((cbuf->head + 1) % CAN_CIRCLE_BUF_SIZE) == cbuf->tail;
}

static inline int canfd_cbuf_push(struct canfd_frame_circlebuf *cbuf,
                                  const struct canfd_frame *frame) {
  if (canfd_cbuf_is_full(cbuf)) {
    return -1; // 满
  }
  memcpy(&cbuf->buf[cbuf->head], frame, sizeof(struct canfd_frame));
  cbuf->head = (cbuf->head + 1) % CAN_CIRCLE_BUF_SIZE;
  cbuf->ready = 1;
  return 0;
}

static inline int canfd_cbuf_pop(struct canfd_frame_circlebuf *cbuf,
                                 struct canfd_frame *frame) {
  if (canfd_cbuf_is_empty(cbuf)) {
    cbuf->ready = 0;
    return -1; // 空
  }
  memcpy(frame, &cbuf->buf[cbuf->tail], sizeof(struct canfd_frame));
  cbuf->tail = (cbuf->tail + 1) % CAN_CIRCLE_BUF_SIZE;
  if (cbuf->tail == cbuf->head) {
    cbuf->ready = 0;
  }
  return 0;
}

struct canfd_frame_circlebuf can_rx_cbuf = {.head = 0, .tail = 0, .ready = 0};
struct canfd_frame_circlebuf can_tx_cbuf = {.head = 0, .tail = 0, .ready = 0};

// FlexCAN消息与canfd_frame转换
static inline void flexcan_msg_to_canfd_frame(const Flexcan_Ip_MsgBuffType *msg,
                                              struct canfd_frame *frame) {
  bool is_ext = !!(msg->cs & FLEXCAN_IP_CS_IDE_MASK);
  bool is_fdf = !!(msg->cs & FLEXCAN_IP_MB_EDL_MASK);
  bool is_brs = !!(msg->cs & FLEXCAN_IP_MB_BRS_MASK);
  bool is_rmt = !!(msg->cs & FLEXCAN_IP_CS_RTR_MASK);
  uint8_t dlc = (msg->cs & FLEXCAN_IP_CS_DLC_MASK) >> FLEXCAN_IP_CS_DLC_SHIFT;
  const uint8_t dlc2len[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                               8, 12, 16, 20, 24, 32, 48, 64};
  frame->can_id =
      msg->msgId | (is_ext ? CAN_EFF_FLAG : 0) | (is_rmt ? CAN_RTR_FLAG : 0);
  frame->len = dlc2len[dlc];
  frame->flags = (is_fdf ? CANFD_FDF : 0) | (is_brs ? CANFD_BRS : 0);
  memcpy(frame->data, msg->data, frame->len);
}

// canfd_frame 到 FlexCAN 发送参数的转换
static inline void canfd_frame_to_tx(const struct canfd_frame *frame,
                                     Flexcan_Ip_DataInfoType *tx_info,
                                     uint32_t *msg_id, uint8_t *data) {
  tx_info->msg_id_type =
      (frame->can_id & CAN_EFF_FLAG) ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
  tx_info->data_length = frame->len;
  tx_info->is_remote = (frame->can_id & CAN_RTR_FLAG) ? true : false;
  tx_info->is_polling = false;
  tx_info->enable_brs = (frame->flags & CANFD_BRS) ? true : false;
  tx_info->fd_enable =
      (tx_info->enable_brs || (frame->flags & CANFD_FDF)) ? true : false;
  tx_info->fd_padding = 0;
  *msg_id = frame->can_id & CAN_EFF_FLAG ? (frame->can_id & CAN_EFF_MASK)
                                         : (frame->can_id & CAN_SFF_MASK);
  memcpy(data, frame->data, frame->len);
}

extern void FlexCAN_UserCallback(uint8 instance, Flexcan_Ip_EventType eventType,
                                 uint32 buffIdx,
                                 const Flexcan_Ip_StateType *flexcanState) {
  (void)flexcanState;
  if (FLEXCAN_EVENT_RX_COMPLETE == eventType) {
    if (buffIdx >= sizeof(can_rx_msg) / sizeof(can_rx_msg[0])) {
      // 错误处理: 缓冲区索引超出范围
      return;
    }
    struct canfd_frame frame;
    flexcan_msg_to_canfd_frame(&can_rx_msg[buffIdx], &frame);
    canfd_cbuf_push(&can_rx_cbuf, &frame);
    // re-enable reception for the next message
    FlexCAN_Ip_Receive(instance, buffIdx, &can_rx_msg[buffIdx], false);
  } else if (FLEXCAN_EVENT_TX_COMPLETE == eventType) {
    // 发送完成, 检查软件环形缓冲区是否有待发送帧
    struct canfd_frame frame;
    if (canfd_cbuf_pop(&can_tx_cbuf, &frame) == 0) {
      Flexcan_Ip_DataInfoType tx_info;
      uint32_t msg_id;
      uint8_t data[64];
      canfd_frame_to_tx(&frame, &tx_info, &msg_id, data);
      int ret = FlexCAN_Ip_Send(instance, TX_MBS[0], &tx_info, msg_id, data);
      if (ret == FLEXCAN_STATUS_SUCCESS) {
        can_tx_busy = 1;
      } else {
        // 发送失败，等待下次中断再尝试
      }
    } else {
      can_tx_busy = 0;
    }
  }
}

extern void FlexCAN_ErrorCallback(uint8 instance,
                                  Flexcan_Ip_EventType eventType,
                                  uint32 u32ErrStatus,
                                  const Flexcan_Ip_StateType *flexcanState) {
  (void)instance;
  (void)eventType;
  (void)u32ErrStatus;
  (void)flexcanState;
}

// Linux SocketCAN 风格的发送函数
int bsp_can_send(int channel, struct canfd_frame *frame) {
  FlexCAN_Ip_DisableInterrupts(channel);
  if (!can_tx_busy) {
    Flexcan_Ip_DataInfoType tx_info;
    uint32_t msg_id;
    uint8_t data[64];
    canfd_frame_to_tx(frame, &tx_info, &msg_id, data);
    int ret =
        FlexCAN_Ip_Send((uint8_t)channel, TX_MBS[0], &tx_info, msg_id, data);
    if (ret == FLEXCAN_STATUS_SUCCESS) {
      can_tx_busy = 1;
      FlexCAN_Ip_EnableInterrupts(channel);
      return 0;
    } else if (ret == FLEXCAN_STATUS_BUFF_OUT_OF_RANGE) {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -1;
    } else if (ret == FLEXCAN_STATUS_BUSY) {
      // 放入软件环形缓冲区
      if (canfd_cbuf_push(&can_tx_cbuf, frame) != 0) {
        FlexCAN_Ip_EnableInterrupts(channel);
        return -4; // 缓冲区满
      }
      FlexCAN_Ip_EnableInterrupts(channel);
      return 1;
    } else {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -3;
    }
  } else {
    // 正在发送，放入软件环形缓冲区
    if (canfd_cbuf_push(&can_tx_cbuf, frame) != 0) {
      FlexCAN_Ip_EnableInterrupts(channel);
      return -4; // 缓冲区满
    }
    FlexCAN_Ip_EnableInterrupts(channel);
    return 0;
  }
}

// less than 100us? delay
void delay(int count) {
  int cnt = count * 1200;
  while (cnt--) {
    __asm__ volatile("nop");
  }
}

int main(void) {
  /* 初始化时钟, 引脚, 中断控制器 */
  Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
  Siul2_Port_Ip_Init(
      NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
      g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);
  IntCtrl_Ip_Init(&IntCtrlConfig_0);

  /* 初始化FlexCAN驱动 */
  FlexCAN_Ip_Init(FLEXCAN_INSTANCE, &FlexCAN_State0, &FlexCAN_Config0);
  // TDC: 60M/5M*0.75 = 9
  FlexCAN_Ip_SetTDCOffset(FLEXCAN_INSTANCE, TRUE, 9U);
  // rx filter, mb0: std, mb1: ext, id:mask = 0:0
  FlexCAN_Ip_SetRxMaskType(FLEXCAN_INSTANCE, FLEXCAN_RX_MASK_INDIVIDUAL);
  FlexCAN_Ip_SetRxIndividualMask(FLEXCAN_INSTANCE, RX_STD_MB_IDX, 0x0U);
  FlexCAN_Ip_ConfigRxMb(FLEXCAN_INSTANCE, RX_STD_MB_IDX, &rx_std_info, 0x0U);
  FlexCAN_Ip_SetRxIndividualMask(FLEXCAN_INSTANCE, RX_EXT_MB_IDX, 0x0U);
  FlexCAN_Ip_ConfigRxMb(FLEXCAN_INSTANCE, RX_EXT_MB_IDX, &rx_ext_info, 0x0U);
  // start FlexCAN
  FlexCAN_Ip_SetStartMode(FLEXCAN_INSTANCE);

  // 开始接收
  FlexCAN_Ip_Receive(FLEXCAN_INSTANCE, RX_STD_MB_IDX, &can_rx_msg[0], false);
  FlexCAN_Ip_Receive(FLEXCAN_INSTANCE, RX_EXT_MB_IDX, &can_rx_msg[1], false);

  // 发送测试, 合计 9+9+65+65 = 148 帧
  struct canfd_frame frame;
  int i;

  // 0~8字节标准CAN帧
  for (i = 0; i <= 8; i++) {
    frame.can_id = 0x123 + i;
    frame.len = i;
    frame.flags = 0;
    memset(frame.data, 0xA5, i);
    while (bsp_can_send(FLEXCAN_INSTANCE, &frame) != 0) {
      // delay(1);
    }
  }

  // 0~8字节扩展CAN帧
  for (i = 0; i <= 8; i++) {
    frame.can_id = (0x12345678 | CAN_EFF_FLAG) + i;
    frame.len = i;
    frame.flags = 0;
    memset(frame.data, 0x5A, i);
    while (bsp_can_send(FLEXCAN_INSTANCE, &frame) != 0) {
      // delay(1);
    }
  }

  // CANFD 不带BRS 测试略

  // 0~64字节 CANFD/CANFD_BRS 标准帧
  for (i = 0; i <= 64; i++) {
    frame.can_id = 0x321 + i;
    frame.len = i;
    frame.flags = CANFD_FDF | CANFD_BRS;
    memset(frame.data, 0x11, i);
    while (bsp_can_send(FLEXCAN_INSTANCE, &frame) != 0) {
      // delay(1);
    }
  }

  // 0~64字节 CANFD/CANFD_BRS 扩展帧
  for (i = 0; i <= 64; i++) {
    frame.can_id = (0x12ABCEDF | CAN_EFF_FLAG) + i;
    frame.len = i;
    frame.flags = CANFD_FDF | CANFD_BRS;
    memset(frame.data, 0x22, i);
    while (bsp_can_send(FLEXCAN_INSTANCE, &frame) != 0) {
      // delay(1);
    }
  }

  while (1) {
    // 检查是否有接收到的帧
    struct canfd_frame frame;
    while (canfd_cbuf_pop(&can_rx_cbuf, &frame) == 0) {
      // echo 回去
      while (bsp_can_send(FLEXCAN_INSTANCE, &frame) != 0) {
        delay(1);
      }
    }
  }

  return 0;
}
