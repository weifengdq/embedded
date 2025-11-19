#include "main.h"
#include "gd32a50x.h"
#include "systick.h"


int main(void) {
  systick_config();

  // PC0 LED
  rcu_periph_clock_enable(RCU_GPIOC);
  gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_0);
  gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_0);
  // 低电平熄灭, 以下方法都可以
  // GPIO_BCR(GPIOC) = GPIO_PIN_0;
  // gpio_bit_reset(GPIOC, GPIO_PIN_0);
  gpio_bit_write(GPIOC, GPIO_PIN_0, RESET);

  while (1) {
    gpio_bit_toggle(GPIOC, GPIO_PIN_0);
    delay_1ms(500);
  }
}