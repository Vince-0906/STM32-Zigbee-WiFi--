#include "drv_tim.h"
#include "stm32f10x.h"

static volatile uint32_t s_ms = 0;

void SysTick_Handler(void)
{
    s_ms++;
}

void drv_tim_init(void)
{
    /* SysTick = HCLK / 8 = 9MHz；1ms 需 9000 */
    SysTick_Config(SystemCoreClock / 1000);
}

uint32_t ms_now(void)
{
    return s_ms;
}

void drv_delay_ms(uint32_t ms)
{
    uint32_t t0 = s_ms;
    while ((s_ms - t0) < ms) { __NOP(); }
}
