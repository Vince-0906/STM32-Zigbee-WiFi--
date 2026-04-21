#ifndef __DRV_GPIO_H__
#define __DRV_GPIO_H__

#include "stm32f10x.h"

#ifdef __cplusplus
extern "C" {
#endif

void drv_gpio_init_board(void);        /* LED/KEY/ZB RESET/WiFi EN */

/* 板上 LED（PC13/14/15，低有效） */
void led_red_set(uint8_t on);
void led_green_set(uint8_t on);
void led_blue_set(uint8_t on);

/* CC2530 RESET（PC4，低有效），约 1ms 低电平脉冲 */
void cc2530_reset_pulse(void);

/* WiFi EN（PB12） */
void wifi_en_set(uint8_t on);

/* 本地蜂鸣器接口已停用，PA15 不再由本工程接管，也不再暴露控制接口。 */
/*
void buzzer_set(uint8_t on);
*/

/* 按键（PB6/PB7，上拉） */
uint8_t key1_pressed(void);
uint8_t key2_pressed(void);

#ifdef __cplusplus
}
#endif

#endif
