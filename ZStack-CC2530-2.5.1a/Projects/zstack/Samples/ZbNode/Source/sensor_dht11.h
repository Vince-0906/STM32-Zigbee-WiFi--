#ifndef __SENSOR_DHT11_H__
#define __SENSOR_DHT11_H__

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void  dht11_init(void);
/* 返回 0=OK, <0=err。temp_x100 i16 (°C*100)，hum_x100 u16 (%*100)。 */
int8  dht11_read(int16 *temp_x100, uint16 *hum_x100);

#ifdef __cplusplus
}
#endif

#endif
