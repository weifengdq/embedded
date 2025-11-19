#include "main.h"
#include "gd32a50x.h"
#include "systick.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void) {
  systick_config();
  uart0_init(115200U);

  // 96bit Unique ID
  // UID0[31:0]   at 0x1FFFF7E8
  // UID1[63:32]  at 0x1FFFF7EC
  // UID2[95:64]  at 0x1FFFF7F0
  uint32_t uid0 = *(uint32_t *)0x1FFFF7E8;
  uint32_t uid1 = *(uint32_t *)0x1FFFF7EC;
  uint32_t uid2 = *(uint32_t *)0x1FFFF7F0;
  printf("Unique ID: %08X-%08X-%08X\n", uid2, uid1, uid0);

  while (1) {
  }
}