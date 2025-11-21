#include "main.h"
#include "gd32a50x.h"
#include "lin.h"
#include "systick.h"

// LIN 驱动需要的全局变量
lin_parameter g_lin_node;
volatile uint32_t wait_timeout = 0;

int main(void) {
  systick_config();

  // PC15 LED (初始熄灭，低电平点亮)
  rcu_periph_clock_enable(RCU_GPIOC);
  gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_15);
  gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_15);
  gpio_bit_write(GPIOC, GPIO_PIN_15, SET); // LED 熄灭

  // WK Button: PA0
  rcu_periph_clock_enable(RCU_GPIOA);
  gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO_PIN_0);

  // LIN0: PA3 TX, PA4 RX
  lin_init(LIN0);

  lin_msg txmsg = {
      .pid = lin_pid_calculate(FRAME_MASTER_REQ),
      .length = 8,
      .data = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0},
      .checksum = 0,
      .error = NO_ERR,
      .task_type = TASK_TYPE_TX,
  };

  // 初始化 g_lin_node 和 msg_list
  g_lin_node.lin_periph = LIN0;
  g_lin_node.work_mode = LIN_MASTER;
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
  
  // 配置要发送的消息（ID = FRAME_MASTER_REQ）
  uint8_t tx_id = FRAME_MASTER_REQ;
  g_lin_node.msg_list[tx_id] = txmsg;
  
  // 初始化 msg_readback
  g_lin_node.msg_readback.error = NO_ERR;
  g_lin_node.msg_readback.checksum = 0;
  g_lin_node.msg_readback.pid = 0;

  // 每 500ms 检测一次按键, 如果按键被按下则发送 LIN 帧
  // 注意: WK 按键按下时为 RESET (低电平)
  while (1) {
    delay_1ms(500);
    if (RESET == gpio_input_bit_get(GPIOA, GPIO_PIN_0)) {
      // 按键按下，发送 LIN 帧
      if (ERROR == lin_header_transmit(LIN0, &txmsg)) {
        // 发送失败，跳过本次发送
        // 点亮 LED 表示错误
        gpio_bit_write(GPIOC, GPIO_PIN_15, RESET);
        continue;
      }
      if (ERROR == lin_data_transmit(LIN0, &txmsg)) {
        gpio_bit_write(GPIOC, GPIO_PIN_15, RESET);
        continue;
      }
      if (ERROR == lin_checksum_transmit(LIN0, &txmsg)) {
        gpio_bit_write(GPIOC, GPIO_PIN_15, RESET);
        continue;
      }
      // // 等待按键释放
      // while (RESET == gpio_input_bit_get(GPIOA, GPIO_PIN_0)) {
      //   delay_1ms(10);
      // }
    }
  }
}

void systick_1ms_callback(void) {
  if (wait_timeout > 0) {
    wait_timeout--;
  }
}

void LIN0_IRQHandler(void) {
  uint8_t rx_data;

  /* a break frame detected - 仍需更新状态机供回读校验使用 */
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_LBD)) {
    usart_interrupt_flag_clear(LIN0_PERIPH, USART_INT_FLAG_LBD);
    // 防止 RBNE 先触发导致状态机已推进到 SYNC 后被 LBD 重置回 BREAK
    if (g_lin_node.lin_phase == PHASE_IDLE || g_lin_node.lin_phase == PHASE_BREAK) {
        g_lin_node.lin_phase = PHASE_BREAK;
        g_lin_node.status = 0;
    }
  }

  /* a character received - 回读校验 */
  if (RESET != usart_interrupt_flag_get(LIN0_PERIPH, USART_INT_FLAG_RBNE)) {
    rx_data = usart_data_receive(LIN0_PERIPH);

    // Master 回读发送的字符进行校验
    switch(g_lin_node.lin_phase) {
      case PHASE_BREAK:
        // Break 后收到回读，进入 SYNC 阶段
        g_lin_node.lin_phase = PHASE_SYNC;
        break;
      case PHASE_SYNC:
        // 回读校验 SYNC (0x55)
        if(rx_data == 0x55) {
          g_lin_node.lin_phase = PHASE_PID;
        } else {
          g_lin_node.status |= (1 << 1); // SYNC 错误
        }
        break;
      case PHASE_PID:
        // 回读校验 PID
        g_lin_node.msg_readback.pid = rx_data;
        g_lin_node.lin_phase = PHASE_DATA0;
        break;
      case PHASE_DATA0:
      case PHASE_DATA1:
      case PHASE_DATA2:
      case PHASE_DATA3:
      case PHASE_DATA4:
      case PHASE_DATA5:
      case PHASE_DATA6:
      case PHASE_DATA7:
        // 回读校验数据字节
        g_lin_node.msg_readback.data[g_lin_node.lin_phase - PHASE_DATA0] = rx_data;
        g_lin_node.lin_phase++;
        break;
      case PHASE_CHECKSUM:
        // 回读校验 Checksum
        g_lin_node.msg_readback.checksum = rx_data;
        g_lin_node.lin_phase = PHASE_IDLE;
        break;
      default:
        break;
    }
  }

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