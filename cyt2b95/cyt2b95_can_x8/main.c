#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include "cyt2b95cae.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

cy_stc_scb_uart_context_t uart0_context;
uint8_t g_uart_out_data[256];

// 8路CAN配置指针数组
const cy_stc_canfd_config_t *canfd_configs[8] = {
    &canfd_0_chan_0_config, &canfd_0_chan_1_config, &canfd_0_chan_2_config,
    &canfd_0_chan_3_config, &canfd_1_chan_0_config, &canfd_1_chan_1_config,
    &canfd_1_chan_2_config, &canfd_1_chan_3_config};

typedef enum {
  CAN0 = 0, // canfd_0_chan_0
  CAN1 = 1, // canfd_0_chan_1
  CAN2 = 2, // canfd_0_chan_2
  CAN3 = 3, // canfd_0_chan_3
  CAN4 = 4, // canfd_1_chan_0
  CAN5 = 5, // canfd_1_chan_1
  CAN6 = 6, // canfd_1_chan_2
  CAN7 = 7  // canfd_1_chan_3
} can_channel_t;

CANFD_Type *get_canfd_instance(uint8_t chan) {
  if (chan <= CAN3) {
    return CANFD0;
  } else {
    return CANFD1;
  }
}

uint8_t get_canfd_channel(uint8_t chan) {
  return chan % 4; // 每个 CANFD 实例有4个通道
}

void print(void *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  vsprintf((char *)&g_uart_out_data[0], (char *)fmt, arg);
  while (Cy_SCB_UART_IsTxComplete(SCB0) != true) {
  };
  Cy_SCB_UART_PutArray(SCB0, g_uart_out_data, strlen((char *)g_uart_out_data));
  va_end(arg);
}

// 8路 CAN 上下文
cy_stc_canfd_context_t can_context[8] = {0};

// linux can
#define CAN_EFF_FLAG 0x80000000U     /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U     /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U     /* error message frame */
#define CAN_SFF_MASK 0x000007FFU     /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU     /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU     /* omit EFF, RTR, ERR flags */
#define CANXL_PRIO_MASK CAN_SFF_MASK /* 11 bit priority mask */
#define CANFD_MAX_DLC 15
#define CANFD_MAX_DLEN 64
#define CANFD_BRS 0x01 /* bit rate switch (second bitrate for payload data) */
#define CANFD_ESI 0x02 /* error state indicator of the transmitting node */
#define CANFD_FDF 0x04 /* mark CAN FD for dual use of struct canfd_frame */
typedef uint32_t canid_t;
struct canfd_frame {
  canid_t can_id; /* 32 bit CAN_ID + EFF/RTR/ERR flags */
  uint8_t len;    /* frame payload length in byte */
  uint8_t flags;  /* additional flags for CAN FD */
  uint8_t __res0; /* reserved / padding */
  uint8_t __res1; /* reserved / padding */
  uint8_t data[CANFD_MAX_DLEN];
};
typedef struct canfd_frame canfd_frame_t;
#define CANFD_MTU (sizeof(struct canfd_frame))

// can_len2dlc
static uint8_t can_len2dlc(uint8_t len) {
  if (len <= 8) {
    return len;
  } else if (len <= 12) {
    return 9;
  } else if (len <= 16) {
    return 10;
  } else if (len <= 20) {
    return 11;
  } else if (len <= 24) {
    return 12;
  } else if (len <= 32) {
    return 13;
  } else if (len <= 48) {
    return 14;
  } else {
    return 15;
  }
}

// can_dlc2len
static uint8_t can_dlc2len[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64,
};

static uint8_t can_tx_index[8] = {0};

int can_send(uint8_t chan, canfd_frame_t *frame) {
  // 创建实际的结构体对象 (不是指针!)
  cy_stc_canfd_t0_t t0_register = {0};
  cy_stc_canfd_t1_t t1_register = {0};
  uint32_t data[CY_CANFD_MESSAGE_DATA_BUFFER_SIZE] = {0};

  // 设置 T0 寄存器
  t0_register.id = frame->can_id & CAN_EFF_MASK;
  if (frame->can_id & CAN_EFF_FLAG) {
    t0_register.xtd = CY_CANFD_XTD_EXTENDED_ID;
  } else {
    t0_register.xtd = CY_CANFD_XTD_STANDARD_ID;
  }
  if (frame->can_id & CAN_RTR_FLAG) {
    t0_register.rtr = CY_CANFD_RTR_REMOTE_FRAME;
  } else {
    t0_register.rtr = CY_CANFD_RTR_DATA_FRAME;
  }
  t0_register.esi = CY_CANFD_ESI_ERROR_ACTIVE;

  // 设置 T1 寄存器
  t1_register.dlc = can_len2dlc(frame->len);
  if ((frame->flags & CANFD_FDF) || (frame->flags & CANFD_BRS)) {
    t1_register.fdf = CY_CANFD_FDF_CAN_FD_FRAME;
  } else {
    t1_register.fdf = CY_CANFD_FDF_STANDARD_FRAME;
  }
  t1_register.brs = (frame->flags & CANFD_BRS) ? true : false;
  t1_register.efc = false;
  t1_register.mm = 0;

  // 复制数据
  uint32_t dataWords = (frame->len + 3) / 4; // Round up to nearest word
  for (uint32_t i = 0; i < dataWords && i < CY_CANFD_MESSAGE_DATA_BUFFER_SIZE;
       i++) {
    data[i] = ((uint32_t *)frame->data)[i];
  }

  // 组装 TX Buffer 结构体,指针指向实际对象
  cy_stc_canfd_tx_buffer_t txBuffer = {
      .t0_f = &t0_register, .t1_f = &t1_register, .data_area_f = data};

  // 根据通道选择配置
  const cy_stc_canfd_config_t *config = canfd_configs[chan];

  cy_en_canfd_status_t status = Cy_CANFD_UpdateAndTransmitMsgBuffer(
      get_canfd_instance(chan), get_canfd_channel(chan), &txBuffer,
      can_tx_index[chan], &can_context[chan]);
  can_tx_index[chan] = (can_tx_index[chan] + 1) % config->noOfTxBuffers;
  return (status == CY_CANFD_SUCCESS) ? 0 : -1;
}

// 发送 CAN, CANFD, CANFD_BRS 的 标准帧和扩展帧, 以及CAN的远程帧
void test_can_send(void) {
  // clang-format off
  canfd_frame_t frame[] = {
      { .can_id = 0x120, .len = 8, .flags = 0, .data = {1,2,3,4,5,6,7,8} },  // CAN 标准帧
      { .can_id = 0x1FFFFFF0 | CAN_EFF_FLAG, .len = 8, .flags = 0, .data = {1,2,3,4,5,6,7,8} },  // CAN 扩展帧
      { .can_id = 0x121 | CAN_RTR_FLAG, .len = 8, .flags = 0, .data = {0} },  // CAN 远程帧
      { .can_id = 0x122, .len = 32, .flags = CANFD_FDF, .data = {1,2,3,4,5,6,7,8,
                                                                 9,10,11,12,13,14,15,16,
                                                                 17,18,19,20,21,22,23,24,
                                                                 25,26,27,28,29,30,31,32} },  // CANFD 标准帧
      { .can_id = 0x1FFFFFF1 | CAN_EFF_FLAG, .len = 64, .flags = CANFD_FDF | CANFD_BRS, .data = {
                                                                 1,2,3,4,5,6,7,8,
                                                                 9,10,11,12,13,14,15,16,
                                                                 17,18,19,20,21,22,23,24,
                                                                 25,26,27,28,29,30,31,32,
                                                                 33,34,35,36,37,38,39,40,
                                                                 41,42,43,44,45,46,47,48,
                                                                 49,50,51,52,53,54,55,56,
                                                                 57,58,59,60,61,62,63,64} },  // CANFD_BRS 扩展帧
  };
  // clang-format on
  for (int i = 0; i < sizeof(frame) / sizeof(frame[0]); i++) {
    if (can_send(CAN0, &frame[i]) == 0) {
      print("Sent frame with can_id: 0x%X\n", frame[i].can_id);
    } else {
      print("Failed to send frame with can_id: 0x%X\n", frame[i].can_id);
    }
  }
}

int main(void) {
  cy_rslt_t result = cybsp_init();
  if (result != CY_RSLT_SUCCESS) {
    CY_ASSERT(0);
  }

  __enable_irq();

  Cy_SCB_UART_DeInit(SCB0);
  Cy_SCB_UART_Init(SCB0, &scb_0_config, &uart0_context);
  Cy_SCB_UART_Enable(SCB0);

  // 初始化 8 路 CAN
  for (uint8_t chan = 0; chan < 8; chan++) {
    if (CY_CANFD_SUCCESS !=
        Cy_CANFD_Init(get_canfd_instance(chan), get_canfd_channel(chan),
                      canfd_configs[chan], &can_context[chan])) {
      print("CAN%d Init Failed\r\n", chan);
    }
  }

  print("CYT2B95 CAN x8 Echo Test\r\n");

  cy_stc_canfd_r0_t r0RxBuffer;
  cy_stc_canfd_r1_t r1RxBuffer;
  uint32_t rxData[CY_CANFD_DATA_ELEMENTS_MAX];
  cy_stc_canfd_rx_buffer_t rxBuffer = {/* .r0_f         */ &r0RxBuffer,
                                       /* .r1_f         */ &r1RxBuffer,
                                       /* .data_area_f  */ rxData};

  while (1) {
    // 轮询 8 路 CAN 的 RX FIFO
    for (uint8_t chan = 0; chan < 8; chan++) {
      /* Checks the Rx FIFO 0 fill level */
      if (_FLD2VAL(CANFD_CH_M_TTCAN_RXF0S_F0FL,
                   CANFD_RXF0S(get_canfd_instance(chan),
                               get_canfd_channel(chan))) == 0UL) {
        continue;
      }
      if (CY_CANFD_SUCCESS !=
          (Cy_CANFD_ExtractMsgFromRXBuffer(get_canfd_instance(chan),
                                           get_canfd_channel(chan), true, 0U,
                                           &rxBuffer, &can_context[chan]))) {
        continue;
      }
      canfd_frame_t frame;
      // 填充 canfd_frame 结构体
      frame.can_id = rxBuffer.r0_f->id;
      if (rxBuffer.r0_f->xtd == CY_CANFD_XTD_EXTENDED_ID) {
        frame.can_id |= CAN_EFF_FLAG;
      }
      if (rxBuffer.r0_f->rtr == CY_CANFD_RTR_REMOTE_FRAME) {
        frame.can_id |= CAN_RTR_FLAG;
      }
      frame.len = can_dlc2len[rxBuffer.r1_f->dlc];
      frame.flags = 0;
      if (rxBuffer.r1_f->fdf == CY_CANFD_FDF_CAN_FD_FRAME) {
        frame.flags |= CANFD_FDF;
      }
      if (rxBuffer.r1_f->brs) {
        frame.flags |= CANFD_BRS;
      }
      // 复制数据
      uint32_t dataWords = (frame.len + 3) / 4; // Round up to nearest word
      for (uint32_t i = 0;
           i < dataWords && i < CY_CANFD_MESSAGE_DATA_BUFFER_SIZE; i++) {
        ((uint32_t *)frame.data)[i] = rxBuffer.data_area_f[i];
      }
      print("CAN%d rx can_id: 0x%X, len: %d, flags: %s ", chan, frame.can_id,
            frame.len,
            (frame.flags & CANFD_BRS)   ? "FDBRS"
            : (frame.flags & CANFD_FDF) ? "FD "
                                        : "");
      // 远程帧不再打印数据, 直接显示 remote frame
      if (frame.can_id & CAN_RTR_FLAG) {
        print("remote frame\n");
        continue;
      }
      print("data: ");
      for (uint32_t i = 0; i < frame.len; i++) {
        print("%02X ", frame.data[i]);
      }
      print("\n");

      // Echo: 原封不动发回去
      can_send(chan, &frame);
    }
  }

  return 0;
}