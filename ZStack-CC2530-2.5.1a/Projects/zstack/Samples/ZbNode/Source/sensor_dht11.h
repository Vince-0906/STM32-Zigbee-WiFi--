#ifndef __SENSOR_DHT11_H__
#define __SENSOR_DHT11_H__

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DHT11_OK               ((int8)0)
#define DHT11_ERR_PARAM        ((int8)-1)
#define DHT11_ERR_RESP_LOW     ((int8)-2)
#define DHT11_ERR_RESP_HIGH    ((int8)-3)
#define DHT11_ERR_RESP_END     ((int8)-4)
#define DHT11_ERR_BIT_HIGH     ((int8)-5)
#define DHT11_ERR_BIT_LOW      ((int8)-6)
#define DHT11_ERR_CHECKSUM     ((int8)-7)

void dht11_init(void);
/* Returns 0 on success, <0 on error. temp_x100 is C*100, hum_x100 is %*100. */
int8 dht11_read(int16 *temp_x100, uint16 *hum_x100);

#ifdef __cplusplus
}
#endif

#endif
