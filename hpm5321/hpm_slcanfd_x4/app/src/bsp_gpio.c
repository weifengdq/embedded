#include "bsp_gpio.h"

#include "hpm_gpio_drv.h"

// LED配置结构体
typedef struct {
    uint32_t gpio_oe; // GPIO输出使能寄存器
    uint32_t gpio_do; // GPIO数据输出寄存器
    uint32_t pin;     // 引脚号
    uint8_t invert;   // 是否反相（1=低电平点亮，0=高电平点亮）
} led_config_t;

// LED配置表 - 与pinmux.c保持一致
static const led_config_t led_configs[LED_COUNT] = {
    // LED0: PA02, 高电平点亮
    {GPIO_OE_GPIOA, GPIO_DO_GPIOA, 2, 0},
    // LED1: PA26, 低电平点亮
    {GPIO_OE_GPIOA, GPIO_DO_GPIOA, 26, 1},
    // LED2: PA27, 高电平点亮
    {GPIO_OE_GPIOA, GPIO_DO_GPIOA, 27, 0},
    // LED3: PA28, 低电平点亮
    {GPIO_OE_GPIOA, GPIO_DO_GPIOA, 28, 1},
    // LED4: PA10, 高电平点亮 (修正：应该是PA10而不是PB10)
    {GPIO_OE_GPIOA, GPIO_DO_GPIOA, 10, 0},
    // LED5: PB10, 低电平点亮
    {GPIO_OE_GPIOB, GPIO_DO_GPIOB, 10, 1},
    // LED6: PB09, 高电平点亮
    {GPIO_OE_GPIOB, GPIO_DO_GPIOB, 9, 0},
    // LED7: PB08, 低电平点亮
    {GPIO_OE_GPIOB, GPIO_DO_GPIOB, 8, 1},
};

void bsp_led_init(void)
{
    // 初始化所有LED为熄灭状态
    for (int i = 0; i < LED_COUNT; i++) {
        const led_config_t *config = &led_configs[i];
        // 设置为输出模式，初始状态为熄灭（根据invert标志设置对应的电平）
        gpio_set_pin_output_with_initial(HPM_GPIO0, config->gpio_oe, config->pin, config->invert);
    }
}

void bsp_led_write(led_index_t index, led_state_t state)
{
    if (index >= LED_COUNT) {
        return;
    }

    const led_config_t *config = &led_configs[index];
    // 根据invert标志决定是否反转状态
    uint32_t output_level = config->invert ? !state : state;
    gpio_write_pin(HPM_GPIO0, config->gpio_do, config->pin, output_level);
}

void bsp_led_toggle(led_index_t index)
{
    if (index >= LED_COUNT) {
        return;
    }

    const led_config_t *config = &led_configs[index];
    gpio_toggle_pin(HPM_GPIO0, config->gpio_do, config->pin);
}

led_state_t bsp_led_read(led_index_t index)
{
    if (index >= LED_COUNT) {
        return LED_OFF;
    }

    const led_config_t *config = &led_configs[index];
    uint32_t pin_state = gpio_get_pin_output_status(HPM_GPIO0, config->gpio_do, config->pin);

    // 根据invert标志决定返回值
    return config->invert ? (pin_state ? LED_OFF : LED_ON) : (pin_state ? LED_ON : LED_OFF);
}