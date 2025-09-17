#ifndef __BSP_H__
#define __BSP_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*initialize gpio for LED interface*/
void bsp_init(void);

/*led control functions*/
void bsp_led_blue_on(void);
void bsp_led_blue_off(void);

void bsp_led_green_on(void);
void bsp_led_green_off(void);

void bsp_led_green_toggle(void);
void bsp_led_blue_toggle(void);

bool bsp_button_status(void);

void bsp_send_morse_code(uint32_t bitmask);

#ifdef __cplusplus
}
#endif

#endif /*__BSP_H__*/