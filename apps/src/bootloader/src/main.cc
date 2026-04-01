#include "main.h"

#include <pw_assert/check.h>
#include <pw_log/log.h>
#include <pw_sys_io_stm32cube/init.h>

#include <cstring>

#include "bld_boot.h"
#include "bld_config.h"
#include "bld_engine.h"
#include "bld_storage_flash.h"
#include "bld_transport_uart_dma.h"
#include "gpio.h"
#include "stm32l4xx_hal_flash_ex.h"

UART_HandleTypeDef huart4;
struct bld_uart_dma_ctx g_bld_uart_ctx;
DMA_HandleTypeDef hdma_uart4_rx;

extern "C" {
// System Clock Configuration: 80MHz
void SystemClock_Config(void) {
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

void MX_UART4_Init(void) {
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

void MX_DMA_Init(void) {
  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA2_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel5_IRQn);
}

/* Flash hardware wrapper functions */
int stm32_flash_unlock(void* hw) {
  (void)hw;
  return (HAL_FLASH_Unlock() == HAL_OK) ? 0 : -1;
}

int stm32_flash_lock(void* hw) {
  (void)hw;
  return (HAL_FLASH_Lock() == HAL_OK) ? 0 : -1;
}

int stm32_hal_read(void* hw, uint32_t addr, uint8_t* out, uint32_t len) {
  (void)hw;
  memcpy(out, (const void*)(uintptr_t)addr, len);
  return 0;
}

int stm32_flash_erase_pages(void* hw,
                            uint32_t bank,
                            uint32_t first_page,
                            uint32_t num_pages) {
  uint32_t page_error = 0;
  FLASH_EraseInitTypeDef erase;

  (void)hw;
  memset(&erase, 0, sizeof(erase));
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = bank;
  erase.Page = first_page;
  erase.NbPages = num_pages;

  return (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) ? 0 : -1;
}

int stm32_flash_program_doubleword(void* hw, uint32_t addr, uint64_t data) {
  (void)hw;
  return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data) == HAL_OK)
             ? 0
             : -1;
}

/* UART DMA hardware wrapper functions */
int stm32_uart_rx_start(void* uart, uint8_t* buf, uint16_t len) {
  UART_HandleTypeDef* huart = (UART_HandleTypeDef*)uart;
  return (HAL_UARTEx_ReceiveToIdle_DMA(huart, buf, len) == HAL_OK) ? 0 : -1;
}

int stm32_uart_tx_blocking(void* uart,
                           uint8_t* buf,
                           uint16_t len,
                           uint32_t timeout_ms) {
  UART_HandleTypeDef* huart = (UART_HandleTypeDef*)uart;
  return (HAL_UART_Transmit(huart, buf, len, timeout_ms) == HAL_OK) ? 0 : -1;
}

void stm32_dma_disable_it(void* dma_rx) {
  DMA_HandleTypeDef* hdma = (DMA_HandleTypeDef*)dma_rx;
  __HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT);
}

uint32_t stm32_now_ms(void* time_ctx) {
  (void)time_ctx;
  return HAL_GetTick();
}
}

extern "C" int main(void) {
  HAL_Init();

  SystemClock_Config();
  MX_DMA_Init();
  MX_UART4_Init();
  pw_sys_io_Init();
  bsp_init();

  static const struct bld_uart_dma_ll_ops g_uart_ll_ops = {
      .rx_start = stm32_uart_rx_start,
      .tx_blocking = stm32_uart_tx_blocking,
      .disable_dma_it = stm32_dma_disable_it,
      .now_ms = stm32_now_ms,
  };

  g_bld_uart_ctx.uart = &huart4;
  g_bld_uart_ctx.dma_rx_handle = &hdma_uart4_rx;
  g_bld_uart_ctx.time_ctx = NULL;
  g_bld_uart_ctx.ops = &g_uart_ll_ops;

  bld_uart_dma_start(&g_bld_uart_ctx);
  struct bld_transport transport = bld_transport_uart_dma_make(&g_bld_uart_ctx);

  struct bld_storage slot_storage;
  struct bld_storage meta_storage;

  static const struct bld_flash_ops g_flash_ops = {
      .unlock = stm32_flash_unlock,
      .lock = stm32_flash_lock,
      .read = stm32_hal_read,
      .erase_pages = stm32_flash_erase_pages,
      .program_doubleword = stm32_flash_program_doubleword,
  };

  const struct bld_storage_flash_ctx slot_ctx = {
      .region_base = BLD_SLOT_BASE,
      .region_size = BLD_SLOT_SIZE,
      .page_size = 2048u,
      .flash_base = FLASH_BASE,
      .flash_bank_size = FLASH_BANK_SIZE,
      .flash_page_size = FLASH_PAGE_SIZE,
      .flash_bank1 = FLASH_BANK_1,
      .flash_bank2 = FLASH_BANK_2,
      .ops = &g_flash_ops,
      .hw = NULL,
  };

  const struct bld_storage_flash_ctx meta_ctx = {
      .region_base = BLD_META_BASE,
      .region_size = BLD_META_SIZE,
      .page_size = 2048u,
      .flash_base = FLASH_BASE,
      .flash_bank_size = FLASH_BANK_SIZE,
      .flash_page_size = FLASH_PAGE_SIZE,
      .flash_bank1 = FLASH_BANK_1,
      .flash_bank2 = FLASH_BANK_2,
      .ops = &g_flash_ops,
      .hw = NULL,
  };

  (void)bld_storage_flash_init(&slot_storage, &slot_ctx);
  (void)bld_storage_flash_init(&meta_storage, &meta_ctx);

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
