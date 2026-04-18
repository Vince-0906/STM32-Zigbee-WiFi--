#ifndef __APP_ROUTER_H__
#define __APP_ROUTER_H__

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t  temp_x100;
    uint16_t hum_x100;
    uint16_t lux;
    uint8_t  role;
    uint8_t  dev_type;
    int8_t   rssi;
    uint8_t  last_seen_s;
    uint8_t  led_state;
    uint8_t  buzzer_state;
    uint8_t  online;
    uint32_t last_update_ms;
} node_state_t;

#define ROUTER_NODE_MAX   ZB_NODE_MAX

void  router_init(void);
void  router_on_zb_frame(const frame_t *f);
void  router_on_json_line(const char *line, uint16_t n);

/* 供 automation 查询 / 下发 */
const node_state_t *router_node(uint16_t nodeid);
int   router_cmd_onoff(uint16_t nodeid, uint8_t ep, uint8_t onoff);
uint8_t router_node_count(void);

/* 供 oled_view 取快照（Node1/Node2 固定 nodeid 映射） */
uint16_t router_node1_id(void);
uint16_t router_node2_id(void);

#ifdef __cplusplus
}
#endif

#endif
