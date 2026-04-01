#include "main.h"

// Provide these from your uart init file (extern)
extern UART_HandleTypeDef huart4;

// Provide DMA handles if using DMA
extern DMA_HandleTypeDef hdma_uart4_rx;

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
	if (huart->Instance == UART4) {
		/* USER CODE BEGIN UART4_MspInit 0 */

		/* USER CODE END UART4_MspInit 0 */

		/** Initializes the peripherals clock
  */
		PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART4;
		PeriphClkInit.Uart4ClockSelection = RCC_UART4CLKSOURCE_PCLK1;
		if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
			Error_Handler();
		}

		/* Peripheral clock enable */
		__HAL_RCC_UART4_CLK_ENABLE();

		__HAL_RCC_GPIOA_CLK_ENABLE();
		/**UART4 GPIO Configuration
    PA0     ------> UART4_TX
    PA1     ------> UART4_RX
    */
		GPIO_InitStruct.Pin = ARD_D1_Pin | ARD_D0_Pin;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		/* UART4 DMA Init */
		/* UART4_RX Init */
		hdma_uart4_rx.Instance = DMA2_Channel5;
		hdma_uart4_rx.Init.Request = DMA_REQUEST_2;
		hdma_uart4_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
		hdma_uart4_rx.Init.PeriphInc = DMA_PINC_DISABLE;
		hdma_uart4_rx.Init.MemInc = DMA_MINC_ENABLE;
		hdma_uart4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
		hdma_uart4_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
		hdma_uart4_rx.Init.Mode = DMA_NORMAL;
		hdma_uart4_rx.Init.Priority = DMA_PRIORITY_LOW;
		if (HAL_DMA_Init(&hdma_uart4_rx) != HAL_OK) {
			Error_Handler();
		}

		__HAL_LINKDMA(huart, hdmarx, hdma_uart4_rx);

		/* USER CODE BEGIN UART4_MspInit 1 */

		/* USER CODE END UART4_MspInit 1 */
	}
}