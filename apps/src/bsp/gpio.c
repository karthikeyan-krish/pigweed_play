/*Board Support Package (bsp) for the B-L475E-IOT01A1 board*/
#include "gpio.h"

#include "stm32l4xx_hal.h"

#define LED_GREEN 14
#define LED_BLUE 9
#define B2_PIN 13
#define ARD_D3 0

void bsp_init(void)
{
	RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN);
	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
	/*clear mode bits*/
	GPIOB->MODER &= ~(3U << (LED_GREEN * 2));
	/*setas general-purpose output mode*/
	GPIOB->MODER |= (1U << (LED_GREEN * 2));
	/*configure as push-pull output*/
	GPIOB->OTYPER &= ~(1U << LED_GREEN);
	/*set to high-speed mode*/
	GPIOB->OSPEEDR |= (1U << ((LED_GREEN * 2) + 1));
	/*clear pull-up and pull-down*/
	GPIOB->PUPDR &= ~(3U << (2 * LED_GREEN));
	/*disable pull-up and pull-down*/
	GPIOB->PUPDR |= (1U << (2 * LED_GREEN));

	/*clear mode bits*/
	GPIOC->MODER &= ~(3U << (LED_BLUE * 2));
	/*set as general-purpose output mode*/
	GPIOC->MODER |= (1U << (LED_BLUE * 2));
	/*configure as push-pull output*/
	GPIOC->OTYPER &= ~(1U << LED_BLUE);
	/*set to high-speed mode*/
	GPIOC->OSPEEDR |= (1U << ((LED_BLUE * 2) + 1));
	/*clear pull-up and pull-down*/
	GPIOC->PUPDR &= ~(3U << (2 * LED_BLUE));
	/*disable pull-up and pull-down resistors*/
	GPIOC->PUPDR |= (1U << (2 * LED_BLUE));

	/*configure as input*/
	GPIOC->MODER &= ~(3U << 2 * B2_PIN);
	/*clear pull-up and pull-down*/
	GPIOC->PUPDR &= ~(3U << 2 * B2_PIN);
	/*disable pull-up and pull-down resistors*/
	GPIOC->PUPDR |= (1U << 2 * B2_PIN);

	/*clear EXTI13 bits*/
	SYSCFG->EXTICR[3] &= ~(0xF << (1 * 4));
	/*set EXTI13 to port C*/
	SYSCFG->EXTICR[3] |= (2 << (1 * 4));

	/*disable rising edge trigger*/
	EXTI->RTSR1 &= ~(1U << B2_PIN);
	/*disable falling edge trigger*/
	EXTI->FTSR1 &= ~(1U << B2_PIN);
	/*configure falling edge trigger*/
	EXTI->FTSR1 |= (1U << B2_PIN);

	/*clear pending bit for EXTI line 13*/
	EXTI->PR1 = (1U << B2_PIN);

	/*configure interrupt as falling edge*/
	EXTI->IMR1 |= (1U << B2_PIN);

	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
	NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void bsp_led_green_on(void)
{
	GPIOB->BSRR |= (1U << LED_GREEN);
}

void bsp_led_green_off(void)
{
	GPIOB->BSRR |= (1U << (LED_GREEN + 16));
}

void bsp_led_blue_off(void)
{
	GPIOC->BSRR |= (1U << LED_BLUE);
}

void bsp_led_blue_on(void)
{
	GPIOC->BSRR |= (1U << (LED_BLUE + 16));
}

void bsp_led_green_toggle(void)
{
	GPIOB->ODR ^= (1U << LED_GREEN);
}

void bsp_led_blue_toggle(void)
{
	GPIOC->ODR ^= (1U << LED_BLUE);
}

bool bsp_button_status(void)
{
	return (GPIOC->IDR & (1U << B2_PIN)) ? 0 : 1;
}

void bsp_send_morse_code(uint32_t bitmask)
{
	uint32_t volatile delay_ctr;
	enum { DOT_DELAY = 150 };

	for (; bitmask != 0U; bitmask <<= 1) {
		if ((bitmask & (1U << 31)) != 0U) {
			bsp_led_green_on();
		} else {
			bsp_led_green_off();
		}
		for (delay_ctr = DOT_DELAY; delay_ctr != 0U; --delay_ctr) {
		}
	}
	bsp_led_green_off();
	for (delay_ctr = 7 * DOT_DELAY; delay_ctr != 0U; --delay_ctr) {
	}
}