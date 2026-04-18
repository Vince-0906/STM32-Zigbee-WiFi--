#ifndef __BSP_CLOCK_H__
#define __BSP_CLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 切到 HSE + PLL = 72MHz；system_stm32f10x.c 已默认完成配置，此处为门面。 */
void bsp_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif
