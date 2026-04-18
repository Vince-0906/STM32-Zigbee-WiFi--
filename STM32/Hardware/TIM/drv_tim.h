#ifndef __DRV_TIM_H__
#define __DRV_TIM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SysTick 1ms；供 ms_now() 返回自启动以来毫秒数。 */
void     drv_tim_init(void);
uint32_t ms_now(void);
void     drv_delay_ms(uint32_t ms);   /* 基于 ms_now() 轮询，禁用阻塞中断 */

#ifdef __cplusplus
}
#endif

#endif
