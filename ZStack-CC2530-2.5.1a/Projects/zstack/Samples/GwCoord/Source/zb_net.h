#ifndef __ZB_NET_H__
#define __ZB_NET_H__

#include "gw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16 nodeid;            /* 短地址 */
    uint8  ieee[8];           /* 扩展地址 */
    uint8  type;              /* 0=unknown 1=Router 2=EndDev */
    int8   rssi;              /* 最近 RSSI dBm */
    uint32 last_seen_ms;
    uint8  flags;             /* bit0=online, bit1=hb_sent_nowait */
    uint8  miss_cnt;          /* 心跳连续失败数 */
} zb_node_t;

void  zb_net_init(void);
void  zb_net_tick(uint32 now_ms);

/* 入网窗口；sec=0 关闭，0xFF 常开。 */
void  zb_net_allow_join(uint8 sec);

/* 节点表接口 */
zb_node_t *zb_net_find(uint16 nodeid);
zb_node_t *zb_net_touch(uint16 nodeid, const uint8 *ieee, uint8 type, int8 rssi);
uint8      zb_net_count(void);
const zb_node_t *zb_net_at(uint8 idx);
void       zb_net_mark_offline(uint16 nodeid);

/* 下行 ZCL On/Off（cluster=0x0006）到指定节点/端点；0=OK, <0=err。 */
int8  zb_net_send_onoff(uint16 nodeid, uint8 ep, uint8 onoff);

/* 通知 ZDO 状态变更（由 GwCoord 在 ZDO_STATE_CHANGE 回调里转发） */
void  zb_net_on_state(uint8 state, uint16 panid, uint8 channel);

/* 当前网络快照 */
void  zb_net_get_status(uint8 *state, uint8 *channel, uint16 *panid, uint8 *joined_n);

#ifdef __cplusplus
}
#endif

#endif /* __ZB_NET_H__ */
