#include "main.h"
#include "gd32a50x.h"
#include "lin.h"
#include "systick.h"

// LIN 驱动需要的全局变量
lin_parameter g_lin_node;
volatile uint32_t wait_timeout = 0;

// lin_data_idx
volatile uint8_t lin_data_idx = 0;
// lin_data_buf
volatile uint8_t lin_data_buf[8];

int main(void) {
  systick_config();

  // PC15 LED (初始熄灭，低电平点亮)
  rcu_periph_clock_enable(RCU_GPIOC);
  gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_15);
  gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_15);
  gpio_bit_write(GPIOC, GPIO_PIN_15, SET); // LED 熄灭

  // 初始化 LIN0: PA3 TX, PA4 RX
  lin_init(LIN0);

  // 初始化 g_lin_node 和 msg_list
  g_lin_node.lin_periph = LIN0;
  g_lin_node.work_mode = LIN_SLAVE;
  g_lin_node.lin_phase = PHASE_IDLE;
  g_lin_node.status = 0;
  g_lin_node.current_id = 0;
  
  // 初始化 msg_list 数组（参考官方示例）
  for (uint8_t i = 0; i < 64; i++) {
    g_lin_node.msg_list[i].pid = lin_pid_calculate(i);
    g_lin_node.msg_list[i].length = 8;
    for (uint8_t j = 0; j < 8; j++) {
      g_lin_node.msg_list[i].data[j] = 0;
    }
    g_lin_node.msg_list[i].checksum = 0;
    g_lin_node.msg_list[i].error = NO_ERR;
    g_lin_node.msg_list[i].task_type = TASK_TYPE_NONE;
  }
  
  // 初始化 msg_readback
  g_lin_node.msg_readback.error = NO_ERR;
  g_lin_node.msg_readback.checksum = 0;
  g_lin_node.msg_readback.pid = 0;

  while (1) {
    // 如果收到完整的 LIN 帧，则翻转 LED
    if (g_lin_node.status & (1 << 0)) { // Bit 0 表示接收完成
      g_lin_node.status &= ~(1 << 0);
      
      // 将 msg_readback 数据复制到 msg_list（参考官方示例）
      uint8_t current_id = g_lin_node.current_id;
      if (current_id < 64) {
        for (uint8_t i = 0; i < 8; i++) {
          g_lin_node.msg_list[current_id].data[i] = g_lin_node.msg_readback.data[i];
        }
        g_lin_node.msg_list[current_id].checksum = g_lin_node.msg_readback.checksum;
        g_lin_node.msg_list[current_id].error = g_lin_node.msg_readback.error;
      }
      
      gpio_bit_toggle(GPIOC, GPIO_PIN_15);
    }
    delay_1ms(10);
  }
}

void systick_1ms_callback(void) {
  if (wait_timeout > 0) {
    wait_timeout--;
  }
}

void LIN0_IRQHandler(void) {
  uint8_t rx_data;

  /* a break frame detected - 帧起始 */
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_LBD)) {
    usart_interrupt_flag_clear(LIN0_PERIPH, USART_INT_FLAG_LBD);

    // Break 帧检测，开始新的一帧
    g_lin_node.lin_phase = PHASE_BREAK;
    lin_data_idx = 0;
    g_lin_node.status = 0;
  }

  /* a character received */
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_RBNE)) {
    rx_data = usart_data_receive(LIN0_PERIPH);

    switch (g_lin_node.lin_phase) {
    case PHASE_BREAK:
      // 接收 Break 字符 (0x00)
      if (rx_data == 0x00) {
        g_lin_node.lin_phase = PHASE_SYNC;
      } else if (rx_data == 0x55) {
        // 容错：如果错过了 0x00，直接收到 0x55
        g_lin_node.lin_phase = PHASE_PID;
      } else {
        g_lin_node.lin_phase = PHASE_IDLE;
      }
      break;

    case PHASE_SYNC:
      // 期望收到 0x55
      if (rx_data == 0x55) {
        g_lin_node.lin_phase = PHASE_PID;
      } else {
        g_lin_node.lin_phase = PHASE_IDLE;
      }
      break;

    case PHASE_PID:
      // 接收 PID
      g_lin_node.msg_readback.pid = rx_data;
      g_lin_node.lin_phase = PHASE_DATA0;
      lin_data_idx = 0;
      break;

    case PHASE_DATA0:
    case PHASE_DATA1:
    case PHASE_DATA2:
    case PHASE_DATA3:
    case PHASE_DATA4:
    case PHASE_DATA5:
    case PHASE_DATA6:
    case PHASE_DATA7:
      // 接收数据字节
      lin_data_buf[lin_data_idx++] = rx_data;
      g_lin_node.lin_phase++;
      // 假设固定 8 字节，收满后进入校验和阶段
      if (lin_data_idx >= 8) {
        g_lin_node.lin_phase = PHASE_CHECKSUM;
      }
      break;

    case PHASE_CHECKSUM:
      // 接收到校验和，完成一帧
      g_lin_node.msg_readback.checksum = rx_data;
      
      // 拷贝接收的数据到 msg_readback
      for (uint8_t i = 0; i < 8; i++) {
        g_lin_node.msg_readback.data[i] = lin_data_buf[i];
      }
      
      // 提取 ID（PID 的低 6 位）
      g_lin_node.current_id = g_lin_node.msg_readback.pid & 0x3F;
      
      // 设置接收完成标志（Bit 0）
      g_lin_node.status |= (1 << 0);
      g_lin_node.lin_phase = PHASE_IDLE;
      break;

    default:
      g_lin_node.lin_phase = PHASE_IDLE;
      break;
    }
  }

  /* 错误处理 */
  if (RESET !=
      usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_ERR_ORERR)) {
    usart_interrupt_flag_clear(LIN0_PERIPH, USART_INT_FLAG_ERR_ORERR);
  }
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_PERR)) {
    usart_interrupt_flag_clear(LIN0_PERIPH, USART_INT_FLAG_PERR);
  }
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_ERR_FERR)) {
    usart_interrupt_flag_clear(LIN0_PERIPH, USART_INT_FLAG_ERR_FERR);
  }
}