#include "bsp_clock.h"
#include "stm32f10x.h"

void bsp_clock_init(void)
{
    /* system_stm32f10x.c 的 SystemInit() 在 startup 汇编中已调用。
     * 这里只复位 RCC 到已知状态、保证 AFIO/GPIO 时钟开启。
     */
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_AFIO  |
        RCC_APB2Periph_GPIOA |
        RCC_APB2Periph_GPIOB |
        RCC_APB2Periph_GPIOC |
        RCC_APB2Periph_GPIOD, ENABLE);
}
