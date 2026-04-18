#include "drv_gpio.h"
#include "drv_tim.h"

void drv_gpio_init_board(void)
{
    GPIO_InitTypeDef gi;

    /* PC4 CC2530 RESET - 输出，默认高 */
    gi.GPIO_Pin = GPIO_Pin_4;
    gi.GPIO_Mode = GPIO_Mode_Out_PP;
    gi.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gi);
    GPIO_SetBits(GPIOC, GPIO_Pin_4);

    /* PC13/14/15 LED - 推挽输出，高电平熄灭 */
    gi.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    gi.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &gi);
    GPIO_SetBits(GPIOC, GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);  /* 灭 */

    /* PB12 WiFi EN */
    gi.GPIO_Pin = GPIO_Pin_12;
    gi.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gi);
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);

    /* PA15 BUZZER：上电实测为"高电平鸣叫、低电平静默"（板上 Q1 实际为 NPN 式接法，
     * stm32_board.md §4.10 注明需要实测）。
     * 启动时 PA15 默认被 JTDI 内部上拉钳在高，先把 ODR 置 0 再切推挽输出，
     * 避免 GPIO_Init 瞬间仍是高电平让蜂鸣器长鸣。 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    GPIO_ResetBits(GPIOA, GPIO_Pin_15);
    gi.GPIO_Pin = GPIO_Pin_15;
    gi.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gi);
    GPIO_ResetBits(GPIOA, GPIO_Pin_15);

    /* PB6/PB7 KEY 上拉输入 */
    gi.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    gi.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gi);
}

void led_red_set(uint8_t on)   { if (on) GPIO_ResetBits(GPIOC, GPIO_Pin_13); else GPIO_SetBits(GPIOC, GPIO_Pin_13); }
void led_green_set(uint8_t on) { if (on) GPIO_ResetBits(GPIOC, GPIO_Pin_14); else GPIO_SetBits(GPIOC, GPIO_Pin_14); }
void led_blue_set(uint8_t on)  { if (on) GPIO_ResetBits(GPIOC, GPIO_Pin_15); else GPIO_SetBits(GPIOC, GPIO_Pin_15); }

void cc2530_reset_pulse(void)
{
    GPIO_ResetBits(GPIOC, GPIO_Pin_4);
    drv_delay_ms(1);
    GPIO_SetBits(GPIOC, GPIO_Pin_4);
}

void wifi_en_set(uint8_t on)
{
    if (on) GPIO_SetBits(GPIOB, GPIO_Pin_12);
    else    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
}

void buzzer_set(uint8_t on)
{
    /* 板上实测：PA15 高电平 = 鸣叫，低电平 = 静默（注意与 stm32_board.md §4.10 的注释相反） */
    if (on) GPIO_SetBits(GPIOA, GPIO_Pin_15);
    else    GPIO_ResetBits(GPIOA, GPIO_Pin_15);
}

uint8_t key1_pressed(void) { return (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0); }
uint8_t key2_pressed(void) { return (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7) == 0); }
