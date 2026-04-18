#ifndef __SVC_WIFI_LINK_H__
#define __SVC_WIFI_LINK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_line_cb_t)(const char *line, uint16_t len);

void    wifi_link_init(wifi_line_cb_t cb);
void    wifi_link_poll(uint32_t now_ms);     /* 从 RX 环组装行/透传字节 */
void    wifi_link_tick(uint32_t now_ms);     /* AT 状态机 + 保活 + 重连 */

int     wifi_link_send_line(const char *line, uint16_t n);
uint8_t wifi_link_is_up(void);

#ifdef __cplusplus
}
#endif

#endif
