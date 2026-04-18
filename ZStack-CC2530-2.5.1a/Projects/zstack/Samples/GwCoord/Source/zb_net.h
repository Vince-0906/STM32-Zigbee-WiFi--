#ifndef __ZB_NET_H__
#define __ZB_NET_H__

#include "gw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zb_node_s {
    uint16 nodeid;
    uint8  ieee[8];
    uint8  type;
    uint8  role;
    int8   rssi;
    uint32 last_seen_ms;
    uint8  flags;
    uint8  miss_cnt;
    int16  temp_x100;
    uint16 hum_x100;
    uint16 lux;
    uint8  led_state;
    uint8  buzzer_state;
} zb_node_t;

void  zb_net_init(void);
void  zb_net_tick(uint32 now_ms);
void  zb_net_allow_join(uint8 sec);

zb_node_t *zb_net_find(uint16 nodeid);
zb_node_t *zb_net_touch(uint16 nodeid, const uint8 *ieee, uint8 type, int8 rssi);
const zb_node_t *zb_net_find_by_role(uint8 role);
uint8      zb_net_count(void);
uint8      zb_net_online_count(void);
const zb_node_t *zb_net_at(uint8 idx);
void       zb_net_mark_offline(uint16 nodeid);

int8  zb_net_send_onoff(uint16 nodeid, uint8 ep, uint8 onoff);
void  zb_net_on_state(uint8 state, uint16 panid, uint8 channel);
void  zb_net_get_status(uint8 *state, uint8 *channel, uint16 *panid, uint8 *joined_n);

#ifdef __cplusplus
}
#endif

#endif /* __ZB_NET_H__ */
