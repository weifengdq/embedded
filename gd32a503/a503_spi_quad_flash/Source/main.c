#include "main.h"
#include "gd25qxx.h"
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

ErrStatus memory_compare(uint8_t *src, uint8_t *dst, uint16_t length) {
  while (length--) {
    if (*src++ != *dst++) {
      return ERROR;
    }
  }
  return SUCCESS;
}

#define BUFFER_SIZE 256
#define FLASH_WRITE_ADDRESS 0x000000
#define FLASH_READ_ADDRESS FLASH_WRITE_ADDRESS
uint8_t tx_buffer[BUFFER_SIZE];
uint8_t rx_buffer[BUFFER_SIZE];

int main(void) {
  systick_config();
  uart0_init(115200U);

  printf("GD32A503 SPI Quad Flash 25Q16 Test\r\n");

  spi_flash_init();
  uint32_t flash_id = spi_flash_read_id();
  printf("Flash ID: %06" PRIX32 "\r\n", flash_id);
  // EF4015 is W25Q16, C84015 is GD25Q16
  printf("Flash Type: %s\r\n",
         (flash_id == 0xEF4015)
             ? "W25Q16"
             : ((flash_id == 0xC84015) ? "GD25Q16" : "Unknown"));

  printf("\n\rWrite to tx_buffer:\n\r\n\r");

  /* printf tx_buffer value */
  for (int i = 0; i < BUFFER_SIZE; i++) {
    tx_buffer[i] = i;
    printf("0x%02X ", tx_buffer[i]);

    if (15 == i % 16) {
      printf("\n\r");
    }
  }

  printf("\n\r\n\rRead from rx_buffer:\n\r\n\r");

  /* erase the specified flash sector */
  spi_flash_sector_erase(FLASH_WRITE_ADDRESS);

  /* write tx_buffer data to the flash */
  qspi_flash_buffer_write(tx_buffer, FLASH_WRITE_ADDRESS, 256);
  /* write tx_buffer data to the flash */

  delay_1ms(10);
  /* read a block of data from the flash to rx_buffer */
  qspi_flash_buffer_read(rx_buffer, FLASH_READ_ADDRESS, 256);

  /* printf rx_buffer value */
  for (int i = 0; i < BUFFER_SIZE; i++) {
    printf("0x%02X ", rx_buffer[i]);
    if (15 == i % 16) {
      printf("\n\r");
    }
  }

  uint8_t is_successful = 0;
  /* compare tx_buffer and rx_buffer */
  if (ERROR == memory_compare(tx_buffer, rx_buffer, 256)) {
    printf("\n\rErr:Data Read and Write aren't Matching.\n\r");
    is_successful = 1;
  }

  /* spi qspi flash test passed */
  if (0 == is_successful) {
    printf("\n\rSPI-GD25Q16 Test Passed!\n\r");
  }

  while (1) {
  }
}