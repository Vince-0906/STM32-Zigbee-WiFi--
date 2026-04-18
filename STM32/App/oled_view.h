#ifndef __SVC_OLED_VIEW_H__
#define __SVC_OLED_VIEW_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t zb_ok;
    uint8_t wifi_ok;
    uint8_t pc_ok;
    int16_t n1_temp_x100;
    uint16_t n1_hum_x100;
    uint16_t n2_lux;
    uint8_t  n1_online;
    uint8_t  n2_online;
    char     alm[24];     /* e.g., "LIGHT-ON" / "" */
} oled_model_t;

void oled_view_init(void);
void oled_view_update(const oled_model_t *m);  /* 只更新模型缓存 */
void oled_view_tick(uint32_t now_ms);          /* ≥500ms 节流刷屏 */

#ifdef __cplusplus
}
#endif

#endif
