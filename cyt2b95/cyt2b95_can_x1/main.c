#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

cy_stc_scb_uart_context_t uart0;
uint8_t g_uart_out_data[256];

void print(void *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  vsprintf((char *)&g_uart_out_data[0], (char *)fmt, arg);
  while (Cy_SCB_UART_IsTxComplete(SCB0) != true) {
  };
  Cy_SCB_UART_PutArray(SCB0, g_uart_out_data, strlen((char *)g_uart_out_data));
  va_end(arg);
}

cy_stc_canfd_context_t context = {0};

/* CANFD interrupt handler */
void CanfdInterruptHandler(void) {
  /* Just call the IRQ handler with the current channel number and context */
  Cy_CANFD_IrqHandler(CANFD0, 0, &context);
}
void CAN_RxMsgCallback(bool rxFIFOMsg, uint8_t msgBufOrRxFIFONum,
                       cy_stc_canfd_rx_buffer_t *basemsg) {}

int main(void) {
  cy_rslt_t result = cybsp_init();
  if (result != CY_RSLT_SUCCESS) {
    CY_ASSERT(0);
  }
  __enable_irq();

  Cy_SCB_UART_DeInit(SCB0);
  Cy_SCB_UART_Init(SCB0, &scb_0_config, &uart0);
  Cy_SCB_UART_Enable(SCB0);

  Cy_CANFD_DeInit(CANFD0, 0UL, &context);
  {
#if defined(CPUSS_SYSTEM_IRQ_PRESENT) && (CPUSS_SYSTEM_IRQ_PRESENT == 1)
    cy_stc_sysint_t irq_cfg = {
        /* The upper bits of intrSrc (defined by CY_SYSINT_INTRSRC_MUXIRQ_SHIFT)
          are used to store system interrupt value and the remaining lower bits
          store the CPU IRQ value */
        .intrSrc =
            (IRQn_Type)((NvicMux3_IRQn << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) |
                        canfd_0_interrupts0_1_IRQn),
        .intrPriority = 2UL,
    };
#else
    /* Populate the configuration structure */
    const cy_stc_sysint_t irq_cfg = {
        /* .intrSrc */ canfd_0_interrupts0_0_IRQn, /* CAN FD interrupt number */
        /* .intrPriority */ 3UL};
#endif
    /* Hook the interrupt service routine and enable the interrupt */
    (void)Cy_SysInt_Init(&irq_cfg, &CanfdInterruptHandler);
#if defined(CPUSS_SYSTEM_IRQ_PRESENT) && (CPUSS_SYSTEM_IRQ_PRESENT == 1)
    NVIC_EnableIRQ((IRQn_Type)NvicMux3_IRQn);
#else
    NVIC_EnableIRQ(canfd_0_interrupts0_0_IRQn);
#endif
  }
  if (CY_CANFD_SUCCESS !=
      Cy_CANFD_Init(CANFD0, 0, &canfd_0_chan_0_config, &context)) {
    /* Error processing */
  }
  /* Enables the configuration changes to set Test mode */
  Cy_CANFD_ConfigChangesEnable(CANFD0, 0);
  /* Sets the Test mode configuration */
  Cy_CANFD_TestModeConfig(CANFD0, 0, CY_CANFD_TEST_MODE_DISABLE);
  /* Disables the configuration changes */
  Cy_CANFD_ConfigChangesDisable(CANFD0, 0);

  print("CYT2B95 CAN x1\r\n");

  /* Prepares a CANFD message to transmit */
  uint32_t data[CY_CANFD_MESSAGE_DATA_BUFFER_SIZE];
  /* Sets up the CANFD TX Buffer */
  cy_stc_canfd_t0_t t0registerMsg0 = {
      /* id   */ 0x200,
      /* rtr   */ CY_CANFD_RTR_DATA_FRAME,
      /* xtd   */ (false) ? CY_CANFD_XTD_EXTENDED_ID : CY_CANFD_XTD_STANDARD_ID,
      /* esi */ CY_CANFD_ESI_ERROR_ACTIVE};
  cy_stc_canfd_t1_t t1registerMsg0 = {/* dlc   */ 15U,
                                      /* brs   */ true,
                                      /* fdf   */
                                          (true) ? CY_CANFD_FDF_CAN_FD_FRAME
                                                 : CY_CANFD_FDF_STANDARD_FRAME,
                                      /* efc   */ false,
                                      /* mm    */ 0UL};
  cy_stc_canfd_tx_buffer_t txBuffer = {/* t0_f        */ &t0registerMsg0,
                                       /* t1_f        */ &t1registerMsg0,
                                       /* data_area_f */ data};

  /* Prepare data to send */
  data[0] = 0x78563412UL;
  data[1] = 0x11111111UL;
  data[2] = 0x22222222UL;
  data[3] = 0x33332222UL;
  data[4] = 0x55554444UL;
  data[5] = 0x77776666UL;
  data[6] = 0x99998888UL;
  data[7] = 0xBBBBAAAAUL;
  data[8] = 0xDDDDCCCCUL;
  data[9] = 0xFFFFEEEEUL;
  data[10] = 0x78563412UL;
  data[11] = 0x00000000UL;
  data[12] = 0x11111111UL;
  data[13] = 0x22222222UL;
  data[14] = 0x33333333UL;
  data[15] = 0x44444444UL;

  while (1) {
    /* Sends the prepared data using tx buffer 1 and waits for 1000ms */
    Cy_CANFD_UpdateAndTransmitMsgBuffer(CANFD0, 0u, &txBuffer, 1u, &context);
    Cy_SysLib_Delay(1000u);
  }

  return 0;
}