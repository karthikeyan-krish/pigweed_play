#include "main.h"

#include <pw_assert/check.h>
#include <pw_log/log.h>
#include <pw_sys_io_stm32cube/init.h>

#include <cstring>

#include "bld_boot.h"
#include "bld_config.h"
#include "bld_engine.h"
#include "bld_storage_stm32l4.h"
#include "gpio.h"
#include "transport_uart_dma_idle.h"

UART_HandleTypeDef huart4;
struct bld_uart_dma_ctx g_bld_uart_ctx;
DMA_HandleTypeDef hdma_uart4_rx;

// System Clock Configuration: 80MHz
extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};

  /** Configure the main internal regulator output voltage
   */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

extern "C" void MX_UART4_Init(void) {
  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* Enable UART IRQ for IDLE/event callbacks */
  HAL_NVIC_SetPriority(UART4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
  /* USER CODE END UART4_Init 2 */
}

extern "C" void MX_DMA_Init(void) {
  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA2_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel5_IRQn);
}

extern "C" int main(void) {
  HAL_Init();

  SystemClock_Config();
  MX_DMA_Init();
  MX_UART4_Init();
  pw_sys_io_Init();
  bsp_init();

  memset(&g_bld_uart_ctx, 0, sizeof(g_bld_uart_ctx));
  g_bld_uart_ctx.huart = &huart4;
  bld_uart_dma_start(&g_bld_uart_ctx);

  struct bld_transport transport = bld_transport_uart_dma_make(&g_bld_uart_ctx);

  struct bld_storage slot_storage;
  struct bld_storage meta_storage;

  const struct bld_storage_stm32l4_ctx slot_ctx = {
      .region_base = BLD_SLOT_BASE,
      .region_size = BLD_SLOT_SIZE,
      .page_size = 2048u,
  };

  const struct bld_storage_stm32l4_ctx meta_ctx = {
      .region_base = BLD_META_BASE,
      .region_size = BLD_META_SIZE,
      .page_size = 2048u,
  };

  (void)bld_storage_stm32l4_init(&slot_storage, &slot_ctx);
  (void)bld_storage_stm32l4_init(&meta_storage, &meta_ctx);

  struct bld_engine engine;
  bld_engine_init(&engine, &transport, &slot_storage, &meta_storage);

  /*
   * Boot application unless the user explicitly requests bootloader mode.
   */
  if (!bsp_button_status()) {
    (void)bld_engine_boot_decide_and_jump(&engine);
  }

  PW_LOG_INFO("Bootloader");

  while (1) {
    bld_engine_poll(&engine, 200u);
  }
}

extern "C" PW_NO_RETURN void Error_Handler(void) { PW_CRASH("Error"); }
