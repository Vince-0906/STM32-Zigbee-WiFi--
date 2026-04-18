#ifndef __SVC_CONFIG_H__
#define __SVC_CONFIG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t lux_low;
    int16_t  temp_high_x100;
    int16_t  temp_low_x100;
    uint16_t hum_high_x100;
    uint16_t hum_low_x100;
    uint16_t hysteresis_lux;
    int16_t  hysteresis_temp_x100;
    uint16_t debounce_ms;
} thresholds_t;

int  config_load(thresholds_t *out);
int  config_save(const thresholds_t *in);
void config_default(thresholds_t *out);

#ifdef __cplusplus
}
#endif

#endif
