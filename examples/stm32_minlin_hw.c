#include "minlin/minlin_hw.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_uart.h"

extern UART_HandleTypeDef huart1;

static const uint32_t s_transmit_timeout_per_byte = 5;

// PRIMASK state saved on critical section entry; minlin never nests critical sections.
static uint32_t s_primask;

bool minlin_hw_check_and_clear_break(void) {
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_LBDF)) {
    __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_LBDF);
    return true;
  }
  return false;
}

bool minlin_hw_transmit_break(void) {
  return (HAL_LIN_SendBreak(&huart1) == HAL_OK);
}

// Blocking transmit; the HAL timeout relies on SysTick. If minlin_responder_task() is ever called
// from an interrupt instead of the main loop, that interrupt must have lower priority than both
// SysTick and USART1 (USART1 must still receive the echoed bytes while transmitting).
bool minlin_hw_transmit_data(const uint8_t *data, uint8_t data_size) {
  return (HAL_UART_Transmit(&huart1, data, data_size, s_transmit_timeout_per_byte * data_size) ==
          HAL_OK);
}

void minlin_hw_enter_critical(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_primask = primask;
}

void minlin_hw_exit_critical(void) {
  __set_PRIMASK(s_primask);
}
