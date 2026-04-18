#ifndef __SVC_ZB_LINK_H__
#define __SVC_ZB_LINK_H__

#include <stdint.h>
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zb_link_on_frame_t)(const frame_t *f);

void zb_link_init(zb_link_on_frame_t cb);
void zb_link_poll(uint32_t now_ms);
void zb_link_tick(uint32_t now_ms);

int  zb_link_send(uint8_t cmd, const uint8_t *payload, uint8_t plen);

/* 统计：供日志/OLED 展示 */
uint32_t zb_link_ok_cnt(void);
uint32_t zb_link_err_cnt(void);
uint8_t  zb_link_alive(void);

#ifdef __cplusplus
}
#endif

#endif
