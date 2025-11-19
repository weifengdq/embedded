#include "main.h"
#include "gd32a50x.h"
#include "systick.h"

void mfcom_uart_init(void);
uint8_t mfcom_uart_receive_byte(void);
void mfcom_uart_send_byte(uint8_t data);

int main(void) {
  uint8_t rx_data;

  /* for MFCOM UART clock configuration */
  RCU_CFG0 |= RCU_AHB_CKSYS_DIV2;

  systick_config();

  /* enable GPIO clock */
  rcu_periph_clock_enable(RCU_GPIOA);

  /* configure PA10(TX, MFCOM_D5) and PA11(RX, MFCOM_D4) */
  gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_10 | GPIO_PIN_11);
  gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                          GPIO_PIN_10 | GPIO_PIN_11);
  gpio_af_set(GPIOA, GPIO_AF_6, GPIO_PIN_10 | GPIO_PIN_11);

  /* enable MFCOM clock */
  rcu_periph_clock_enable(RCU_MFCOM);

  /* initialize MFCOM UART */
  mfcom_uart_init();
  mfcom_enable();

  /* UART echo loop */
  while (1) {
    /* receive data */
    rx_data = mfcom_uart_receive_byte();

    /* echo back */
    mfcom_uart_send_byte(rx_data);
  }
}

// initialize mfcom to work in uart mode
void mfcom_uart_init(void) {
  mfcom_timer_parameter_struct mfcom_timer;
  mfcom_shifter_parameter_struct mfcom_shifter;

  /* configure MFCOM_SHIFTER_1 for TX (PA10, MFCOM_D5) */
  mfcom_shifter.timer_select = MFCOM_SHIFTER_TIMER0;
  mfcom_shifter.timer_polarity = MFCOM_SHIFTER_TIMPOL_ACTIVE_HIGH;
  mfcom_shifter.pin_config = MFCOM_SHIFTER_PINCFG_OUTPUT;
  mfcom_shifter.pin_select = MFCOM_SHIFTER_PINSEL_PIN5; /* MFCOM_D5 */
  mfcom_shifter.pin_polarity = MFCOM_SHIFTER_PINPOL_ACTIVE_HIGH;
  mfcom_shifter.mode = MFCOM_SHIFTER_TRANSMIT;
  mfcom_shifter.input_source = MFCOM_SHIFTER_INSRC_PIN;
  mfcom_shifter.stopbit = MFCOM_SHIFTER_STOPBIT_HIGH;
  mfcom_shifter.startbit = MFCOM_SHIFTER_STARTBIT_LOW;
  mfcom_shifter_init(MFCOM_SHIFTER_1, &mfcom_shifter);

  /* configure MFCOM_SHIFTER_3 for RX (PA11, MFCOM_D4) */
  mfcom_shifter.timer_select = MFCOM_SHIFTER_TIMER2;
  mfcom_shifter.timer_polarity = MFCOM_SHIFTER_TIMPOL_ACTIVE_LOW;
  mfcom_shifter.pin_config = MFCOM_SHIFTER_PINCFG_INPUT;
  mfcom_shifter.pin_select = MFCOM_SHIFTER_PINSEL_PIN4; /* MFCOM_D4 */
  mfcom_shifter.pin_polarity = MFCOM_SHIFTER_PINPOL_ACTIVE_HIGH;
  mfcom_shifter.mode = MFCOM_SHIFTER_RECEIVE;
  mfcom_shifter.input_source = MFCOM_SHIFTER_INSRC_PIN;
  mfcom_shifter.stopbit = MFCOM_SHIFTER_STOPBIT_HIGH;
  mfcom_shifter.startbit = MFCOM_SHIFTER_STARTBIT_LOW;
  mfcom_shifter_init(MFCOM_SHIFTER_3, &mfcom_shifter);

  /* configure MFCOM_TIMER_0 for TX */
  mfcom_timer.trigger_select = MFCOM_TIMER_TRGSEL_SHIFTER1;
  mfcom_timer.trigger_polarity = MFCOM_TIMER_TRGPOL_ACTIVE_LOW;
  mfcom_timer.pin_config = MFCOM_TIMER_PINCFG_OUTPUT;
  mfcom_timer.pin_select = MFCOM_TIMER_PINSEL_PIN2;
  mfcom_timer.pin_polarity = MFCOM_TIMER_PINPOL_ACTIVE_HIGH;
  mfcom_timer.mode = MFCOM_TIMER_BAUDMODE;
  mfcom_timer.output = MFCOM_TIMER_OUT_HIGH_EN;
  mfcom_timer.decrement = MFCOM_TIMER_DEC_CLK_SHIFT_OUT;
  mfcom_timer.reset = MFCOM_TIMER_RESET_NEVER;
  mfcom_timer.disable = MFCOM_TIMER_DISMODE_COMPARE;
  mfcom_timer.enable = MFCOM_TIMER_ENMODE_TRIGHIGH;
  mfcom_timer.stopbit = MFCOM_TIMER_STOPBIT_TIMDIS;
  mfcom_timer.startbit = MFCOM_TIMER_STARTBIT_ENABLE;
  mfcom_timer.compare = 0x0FD8;
  mfcom_timer_init(MFCOM_TIMER_0, &mfcom_timer);

  /* configure MFCOM_TIMER_2 for RX */
  mfcom_timer.trigger_select = MFCOM_TIMER_TRGSEL_PIN4;
  mfcom_timer.trigger_polarity = MFCOM_TIMER_TRGPOL_ACTIVE_LOW;
  mfcom_timer.pin_config = MFCOM_TIMER_PINCFG_OUTPUT;
  mfcom_timer.pin_select = MFCOM_TIMER_PINSEL_PIN3;
  mfcom_timer.pin_polarity = MFCOM_TIMER_PINPOL_ACTIVE_HIGH;
  mfcom_timer.mode = MFCOM_TIMER_BAUDMODE;
  mfcom_timer.output = MFCOM_TIMER_OUT_LOW_EN;
  mfcom_timer.decrement = MFCOM_TIMER_DEC_CLK_SHIFT_OUT;
  mfcom_timer.reset = MFCOM_TIMER_RESET_NEVER;
  mfcom_timer.disable = MFCOM_TIMER_DISMODE_COMPARE;
  mfcom_timer.enable = MFCOM_TIMER_ENMODE_TRIGRISING;
  mfcom_timer.stopbit = MFCOM_TIMER_STOPBIT_DISABLE;
  mfcom_timer.startbit = MFCOM_TIMER_STARTBIT_ENABLE;
  mfcom_timer.compare = 0x0FDA;
  mfcom_timer_init(MFCOM_TIMER_2, &mfcom_timer);
}

// receive one byte via MFCOM UART
uint8_t mfcom_uart_receive_byte(void) {
  /* wait for receive complete */
  while (RESET == mfcom_shifter_flag_get(MFCOM_SHIFTER_3))
    ;

  /* read data with bit swap */
  return (uint8_t)mfcom_buffer_read(MFCOM_SHIFTER_3, MFCOM_RWMODE_BITSWAP);
}

// send one byte via MFCOM UART
void mfcom_uart_send_byte(uint8_t data) {
  /* wait for transmit ready */
  while (RESET == mfcom_shifter_flag_get(MFCOM_SHIFTER_1))
    ;

  /* clear the flag before writing */
  // mfcom_shifter_flag_clear(MFCOM_SHIFTER_1);

  /* write data with bit and byte swap */
  mfcom_buffer_write(MFCOM_SHIFTER_1, data, MFCOM_RWMODE_BITBYTESWAP);
}
