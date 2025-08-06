#ifndef BSP_GPIO_H
#define BSP_GPIO_H

typedef enum { LED0 = 0, LED1, LED2, LED3, LED4, LED5, LED6, LED7, LED_COUNT } led_index_t;

// ON OFF
typedef enum { LED_OFF = 0, LED_ON } led_state_t;

#if defined(__cplusplus)
extern "C" {
#endif

void bsp_led_init();
void bsp_led_write(led_index_t index, led_state_t state);
void bsp_led_toggle(led_index_t index);
led_state_t bsp_led_read(led_index_t index);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* BSP_GPIO_H */