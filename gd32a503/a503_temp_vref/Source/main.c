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

void rcu_config(void) {
  /* enable ADC0 clock */
  rcu_periph_clock_enable(RCU_ADC0);
  /* config ADC clock */
  rcu_adc_clock_config(RCU_CKADC_CKAHB_DIV32);
}

static int32_t adc0_temp_calib_value = 0;
void adc_config(void) {
  /* ADC mode config */
  adc_mode_config(ADC_MODE_FREE);
  /* ADC data alignment config */
  adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
  /* ADC continuous function disable */
  adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
  adc_special_function_config(ADC0, ADC_SCAN_MODE, ENABLE);

  /* ADC channel length config */
  adc_channel_length_config(ADC0, ADC_INSERTED_CHANNEL, 2);
  /* ADC regular channel config */
  adc_inserted_channel_config(ADC0, 0, ADC_CHANNEL_16,
                              ADC_SAMPLETIME_479POINT5);
  adc_inserted_channel_config(ADC0, 1, ADC_CHANNEL_17, ADC_SAMPLETIME_55POINT5);

  /* config ADC trigger */
  adc_external_trigger_source_config(ADC0, ADC_INSERTED_CHANNEL,
                                     ADC0_1_EXTTRIG_INSERTED_NONE);
  adc_external_trigger_config(ADC0, ADC_INSERTED_CHANNEL, ENABLE);

  /* enable channel17(Vrefint), channel16(temperature sensor)  */
  adc_vrefint_enable();
  adc_tempsensor_enable();

  /* enable ADC interface */
  adc_enable(ADC0);
  delay_1ms(1);
  /* ADC calibration and reset calibration */
  adc_calibration_enable(ADC0);

  adc0_temp_calib_value = REG16(0x1FFFF7F8) & 0x0FFF;
}

int main(void) {
  systick_config();
  uart0_init(115200U);
  rcu_config();
  adc_config();

  printf("GD32A503 ADC Temperature Sensor and Vrefint Test\r\n");

  int measure_cnt = 0;
  uint32_t temp_sum = 0;
  uint32_t vref_sum = 0;
  while (1) {
    adc_software_trigger_enable(ADC0, ADC_INSERTED_CHANNEL);
    delay_1ms(20);

    // 50 次测量取平均值
    if(measure_cnt < 50) {
      uint16_t temp_val = adc_inserted_data_read(ADC0, 0);
      uint16_t vref_val = adc_inserted_data_read(ADC0, 1);
      temp_sum += temp_val;
      vref_sum += vref_val;
      measure_cnt++;
    } else {
      uint32_t avg_temp = temp_sum / 50;
      uint32_t avg_vref = vref_sum / 50;

      // 计算电压值，单位 mV
      uint32_t vref_voltage = (uint32_t)(5000U * avg_vref) / 4095;

      // 计算温度值，单位 摄氏度
      // 温度传感器参数见数据手册
      float temperature = (avg_temp - adc0_temp_calib_value) * 1000.0f * 5.0f /
                            4095.0f / 4.58f + 30.0f;

      printf("Vref: %" PRIu32 " mV, Temperature: %.2f C\r\n", vref_voltage, temperature);

      // 重置测量
      measure_cnt = 0;
      temp_sum = 0;
      vref_sum = 0;
    }
  }
}