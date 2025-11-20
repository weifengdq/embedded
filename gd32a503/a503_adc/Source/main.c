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

void gpio_config(void) {
  /* enable GPIOD clock */
  rcu_periph_clock_enable(RCU_GPIOD);
  /* config PD9 as analog input for ADC0_IN3 */
  gpio_mode_set(GPIOD, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_9);
}

void rcu_config(void) {
  /* enable ADC0 clock */
  rcu_periph_clock_enable(RCU_ADC0);
  /* config ADC clock */
  rcu_adc_clock_config(RCU_CKADC_CKAHB_DIV32);
}

void adc_config(void) {
  /* ADC mode config */
  adc_mode_config(ADC_MODE_FREE);
  /* ADC data alignment config */
  adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
  /* ADC continuous function disable */
  adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
  adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);

  /* ADC channel length config */
  adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL, 1);
  /* ADC regular channel config - ADC0_IN3 (PD9) */
  adc_regular_channel_config(ADC0, 0, ADC_CHANNEL_3, ADC_SAMPLETIME_55POINT5);

  /* config ADC trigger */
  adc_external_trigger_source_config(ADC0, ADC_REGULAR_CHANNEL,
                                     ADC0_1_EXTTRIG_REGULAR_NONE);
  adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, ENABLE);

  /* enable ADC interface */
  adc_enable(ADC0);
  delay_1ms(1);
  /* ADC calibration and reset calibration */
  adc_calibration_enable(ADC0);
}

int main(void) {
  systick_config();
  uart0_init(115200U);
  gpio_config();
  rcu_config();
  adc_config();

  printf("GD32A503 ADC0_IN3 (PD9) Test\r\n");

  int measure_cnt = 0;
  uint32_t adc_sum = 0;
  while (1) {
    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    delay_1ms(20);

    // 50 次测量取平均值
    if (measure_cnt < 50) {
      uint16_t adc_val = adc_regular_data_read(ADC0);
      adc_sum += adc_val;
      measure_cnt++;
    } else {
      uint32_t avg_adc = adc_sum / 50;

      // 计算电压值，单位 mV
      // Vref = 3.3V (3300mV)
      uint32_t voltage = (uint32_t)(3300U * avg_adc) / 4095;

      printf("ADC0_IN3: %" PRIu32 " (ADC Value: %" PRIu32 "), Voltage: %" PRIu32
             " mV\r\n",
             avg_adc, avg_adc, voltage);

      // 重置测量
      measure_cnt = 0;
      adc_sum = 0;
    }
  }
}