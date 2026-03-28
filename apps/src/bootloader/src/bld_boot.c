#include "bld_boot.h"
#include "stm32l4xx.h"

#define BLD_MSP_POSITION 0u
#define BLD_RESET_HANDLER_POSITION 4u

static const uint8_t bits_per_byte = 8u;

void bld_jump_to_image(uint32_t image_base)
{
	uint32_t app_msp =
		*(volatile uint32_t *)(image_base + BLD_MSP_POSITION);
	uint32_t app_reset =
		*(volatile uint32_t *)(image_base + BLD_RESET_HANDLER_POSITION);

	__disable_irq();

	/* Stop SysTick */
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	/* Disable + clear NVIC */
	for (uint8_t i = 0; i < bits_per_byte; i++) {
		NVIC->ICER[i] = 0xFFFFFFFFu;
		NVIC->ICPR[i] = 0xFFFFFFFFu;
	}

	/* Delocate vector table to app */
	SCB->VTOR = image_base;
	__DSB();
	__ISB();

	__set_MSP(app_msp);

	/* Jump */
	((void (*)(void))app_reset)();
	while (1) {
	}
}
