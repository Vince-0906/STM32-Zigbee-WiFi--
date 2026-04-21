#ifndef __SENSOR_LDR_H__
#define __SENSOR_LDR_H__

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void  ldr_init(void);
/* 返回当前亮度 lux 估算值（uint16，值越大表示越亮）。 */
uint16 ldr_read_lux(void);

#ifdef __cplusplus
}
#endif

#endif
