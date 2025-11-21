#include "main.h"
#include "gd32a50x.h"
#include "systick.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void uart0_init(uint32_t baudrate) {
  rcu_periph_clock_enable(RCU_GPIOA);
  rcu_periph_clock_enable(RCU_USART0);
  rcu_usart_clock_config(USART0, RCU_USARTSRC_CKSYS); // 时钟选择CK_SYS 100MHz

  // UART0, TX PA10, RX PA11
  gpio_af_set(GPIOA, GPIO_AF_5, GPIO_PIN_10);
  gpio_af_set(GPIOA, GPIO_AF_5, GPIO_PIN_11);

  /* configure USART TX as alternate function push-pull */
  gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10);
  gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_10);

  /* configure USART RX as alternate function push-pull */
  gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_11);
  gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_11);

  /* USART configure: 8-N-1*/
  usart_deinit(USART0);
  usart_word_length_set(USART0, USART_WL_8BIT);
  usart_stop_bit_set(USART0, USART_STB_1BIT);
  usart_parity_config(USART0, USART_PM_NONE);
  usart_baudrate_set(USART0, baudrate);
  usart_receive_config(USART0, USART_RECEIVE_ENABLE);
  usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
  usart_enable(USART0);
}

int fputc(int ch, FILE *f) {
  // retarget the C library printf function to the USART
  usart_data_transmit(USART0, (uint8_t)ch);
  while (RESET == usart_flag_get(USART0, USART_FLAG_TBE))
    ;
  return ch;
}

void can_gpio_config(void) {
  /* enable CAN clock */
  rcu_can_clock_config(CAN0, RCU_CANSRC_PCLK2); // 100MHz
  rcu_periph_clock_enable(RCU_CAN0);
  /* enable CAN port clock */
  rcu_periph_clock_enable(RCU_GPIOB);

  /* configure CAN0 GPIO */
  gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
  gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_13);
  gpio_af_set(GPIOB, GPIO_AF_6, GPIO_PIN_13);

  gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_14);
  gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_14);
  gpio_af_set(GPIOB, GPIO_AF_6, GPIO_PIN_14);
}

void can_config(void) {
  can_parameter_struct can_parameter;
  can_fd_parameter_struct fd_parameter;

  /* initialize CAN register */
  can_deinit(CAN0);
  /* initialize CAN */
  can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
  can_struct_para_init(CAN_FD_INIT_STRUCT, &fd_parameter);

  /* initialize CAN parameters */
  can_parameter.internal_counter_source =
      CAN_TIMER_SOURCE_BIT_CLOCK; // internal time counter increase 1 when send
                                  // or receive a bit
  can_parameter.self_reception =
      DISABLE; // receive the frame sent by itself or not
  can_parameter.mb_tx_order =
      CAN_TX_HIGH_PRIORITY_MB_FIRST; // transmit frame priority by MB priority
                                     // or MB number
  can_parameter.mb_tx_abort_enable =
      ENABLE; // support transmission abort function or not
  can_parameter.local_priority_enable =
      DISABLE; // MB priority structure includes local priority value or not
  can_parameter.mb_rx_ide_rtr_type =
      CAN_IDE_RTR_FILTERED; // when receive, (filter IDE && RTR bit), or (always
                            // compare IDE && not compare RTR bit)
  can_parameter.mb_remote_frame =
      CAN_STORE_REMOTE_REQUEST_FRAME; // when receive remote request frame,
                                      // store it as data frame or generate a
                                      // remote response frame
  can_parameter.rx_private_filter_queue_enable =
      DISABLE; // use separate filters / a same filter for all received frames
  can_parameter.edge_filter_enable =
      DISABLE; // used for bus intergration state, enable to detect two
               // continuous norminal dominant bit for hardware synchronous edge
  can_parameter.protocol_exception_enable =
      DISABLE; // enable to detect protocol exception event(when not in FD mode,
               // but receive a FD frame)
  can_parameter.rx_filter_order =
      CAN_RX_FILTER_ORDER_MAILBOX_FIRST; // if RX FIFO is enabled, received
                                         // frame to match mailbox/FIFO first
  can_parameter.memory_size =
      CAN_MEMSIZE_32_UNIT; // 32*4 words specific RAM memory for mailbox and RX
                           // FIFO
  /* filter configuration */
  can_parameter.mb_public_filter =
      0x0; // configure CAN_RMPUBF register (refer to
           // can_private_filter_config() to configure CAN_RFIFOMPFx registers
           // if separate filters are used)
  can_parameter.resync_jump_width = 8;  // SJW
  can_parameter.prop_time_segment = 10; // PTS segment
  can_parameter.time_segment_1 = 21;    // PBS1 segment
  can_parameter.time_segment_2 = 8;     // PBS2 segment
  /* 500Kbps */
  can_parameter.prescaler = 5; // baudrate =
                               // fCANCLK/prescaler/(1+PTS+PBS1+PBS2)

  /* initialize CAN */
  can_init(CAN0, &can_parameter);

  /* FD parameter configurations */
  fd_parameter.bitrate_switch_enable =
      ENABLE; // when transmit frame BRS bit is '1', support to switch bitrate
              // to data bitrate
  fd_parameter.iso_can_fd_enable = ENABLE; // use ISO-CANFD protocol
  fd_parameter.mailbox_data_size =
      CAN_MAILBOX_DATA_SIZE_64_BYTES; // each mailbox with 64 bytes data
  fd_parameter.tdc_enable =
      ENABLE; // disable transmit delay compensation function
  fd_parameter.tdc_offset =
      8; // no more than (1+PTS+PBS1+PBS2)*prescaler tCANCLK
  fd_parameter.resync_jump_width = 2; // SJW
  fd_parameter.prop_time_segment = 1; // PTS segment for FD data bit rate
  fd_parameter.time_segment_1 = 6;    // PBS1 segment for FD data bit rate
  fd_parameter.time_segment_2 = 2;    // PBS2 segment for FD data bit rate
  /* 2Mbps */
  fd_parameter.prescaler = 5; // baudrate = fCANCLK/prescaler/(1+PTS+PBS1+PBS2),
                              // two prescaler should be same

  can_fd_config(CAN0, &fd_parameter);

  /* configure CAN0 NVIC */
  nvic_irq_enable(CAN0_Message_IRQn, 0, 0);

  /* enable CAN MB0 interrupt */
  can_interrupt_enable(CAN0, CAN_INT_MB0);

  can_operation_mode_enter(CAN0, CAN_NORMAL_MODE);
}

volatile FlagStatus can0_receive_flag;
/* Mailbox configured for 64-byte data size in FD mode.
  Ensure rx buffer is large enough to avoid overflow when driver copies payload.
*/
static uint8_t rx_data[64];
static uint8_t dlc2len[] = {0, 1,  2,  3,  4,  5,  6,  7,
                            8, 12, 16, 20, 24, 32, 48, 64};

int main(void) {
  systick_config();
  uart0_init(115200U);
  can_gpio_config();
  can_config();

  printf("GD32A503 CAN0 FD (500K 80%% + 2M 80%%) Test\r\n");

  const uint8_t tx_data[64] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8};
  can_mailbox_descriptor_struct transmit_message;
  can_struct_para_init(CAN_MDSC_STRUCT, &transmit_message);
  /* initialize transmit message */
  transmit_message.rtr = 0;
  transmit_message.ide = 0;
  transmit_message.code = CAN_MB_TX_STATUS_DATA;
  transmit_message.brs = 1;
  transmit_message.fdf = 1;
  transmit_message.esi = 0;
  transmit_message.prio = 0;
  transmit_message.data_bytes = 64;
  /* tx message content */
  transmit_message.data = (uint32_t *)(tx_data);
  transmit_message.id = 0x123;

  can_mailbox_descriptor_struct receive_message;
  can_struct_para_init(CAN_MDSC_STRUCT, &receive_message);
  receive_message.rtr = 0;
  receive_message.ide = 0;
  receive_message.code = CAN_MB_RX_STATUS_EMPTY;
  /* rx mailbox */
  receive_message.id = 0x55;
  receive_message.data = (uint32_t *)(rx_data);
  can_mailbox_config(CAN0, 0, &receive_message);

  // 512B Mesage RAM only can support 7 mailboxes at 64B CANFD
  // index 0 is for rx, and 1,2,3,4,5,6 are for tx
  can_mailbox_config(CAN0, 1, &transmit_message);
  can_mailbox_config(CAN0, 2, &transmit_message);
  can_mailbox_config(CAN0, 3, &transmit_message);
  can_mailbox_config(CAN0, 4, &transmit_message);
  can_mailbox_config(CAN0, 5, &transmit_message);

  while (1) {
    if (SET == can0_receive_flag) {
      can0_receive_flag = RESET;
      /* read received message from MB0 */
      can_mailbox_receive_data_read(CAN0, 0, &receive_message);

      // 打印ID 标准帧3字节, 扩展帧8字节
      if (receive_message.ide == 0) {
        // 标准帧
        printf("id: 0x%03" PRIX32 ", ", receive_message.id & 0x7FF);
      } else {
        // 扩展帧
        printf("id: 0x%08" PRIX32 ", ", receive_message.id & 0x1FFFFFFF);
      }
      // 如果有BRS或者FDF标志也打印出来
      if (receive_message.fdf) {
        printf("FDF ");
      }
      if (receive_message.brs) {
        printf("BRS ");
      }

      printf("len: %d, ", dlc2len[receive_message.dlc]);
      // 远程帧不再打印数据
      if (receive_message.rtr) {
        // 远程帧
        if (receive_message.rtr) {
          printf("RTR ");
        }
      } else {
        printf("data:");
        uint8_t print_len = (dlc2len[receive_message.dlc] <= sizeof(rx_data))
                                ? (uint8_t)dlc2len[receive_message.dlc]
                                : (uint8_t)sizeof(rx_data);
        for (uint8_t i = 0; i < print_len; i++) {
          printf(" %02X", rx_data[i]);
        }
      }
      printf("\r\n");

      // can echo
      transmit_message.id = receive_message.id;
      transmit_message.rtr = receive_message.rtr;
      transmit_message.ide = receive_message.ide;
      transmit_message.brs = receive_message.brs;
      transmit_message.fdf = receive_message.fdf;
      transmit_message.dlc = receive_message.dlc;
      transmit_message.data_bytes = dlc2len[receive_message.dlc];
      transmit_message.data = receive_message.data;
      can_mailbox_config(CAN0, 6, &transmit_message);
    }
  }
}

void CAN0_Message_IRQHandler(void) {
  if (RESET != can_interrupt_flag_get(CAN0, CAN_INT_FLAG_MB0)) {
    can_interrupt_flag_clear(CAN0, CAN_INT_FLAG_MB0);
    can0_receive_flag = SET;
  }
}