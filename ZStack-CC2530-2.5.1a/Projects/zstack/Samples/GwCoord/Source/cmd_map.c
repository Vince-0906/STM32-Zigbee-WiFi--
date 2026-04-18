/*
 * CMD 分发表：上行仅响应本书 §4.5.2 定义的 CMD；未知 → ZB_ERR{UNKNOWN,cmd}。
 */

#include "cmd_map.h"
#include "gw_coord.h"
#include "zb_net.h"

static void do_ping(const frame_t *f)
{
    (void)f;
    (void)GwCoord_SendFrame(CMD_ZB_PING, 0, 0);
}

static void do_allow_join(const frame_t *f)
{
    if (f->plen < 1) { GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_ALLOW_JOIN); return; }
    zb_net_allow_join(f->payload[0]);
}

static void do_list_nodes(const frame_t *f)
{
    uint8 i, cnt;
    uint8 buf[12];
    const zb_node_t *n;
    uint32 now;
    (void)f;
    cnt = zb_net_count();
    now = 0; /* 不需要精确 now，spec 里只要求 last_seen_s 近似 */
    for (i = 0; i < cnt; ++i) {
        n = zb_net_at(i);
        if (n == 0) break;
        buf[0] = (uint8)(n->nodeid & 0xFF);
        buf[1] = (uint8)(n->nodeid >> 8);
        buf[2] = n->type;
        buf[3] = (uint8)n->rssi;
        /* 估算 last_seen_s：当前 ms - last_seen_ms 取秒，饱和到 255 */
        {
            extern uint32 osal_GetSystemClock(void);
            uint32 diff_ms;
            now = osal_GetSystemClock();
            diff_ms = now - n->last_seen_ms;
            buf[4] = (diff_ms > 255000u) ? 0xFFu : (uint8)(diff_ms / 1000u);
        }
        (void)GwCoord_SendFrame(CMD_ZB_NODE_INFO, buf, 5);
    }
    /* 空表也发一个计数=0 的 NET_STATUS 方便 STM32 侧判断 */
    if (cnt == 0) {
        uint8 state, channel, joined_n;
        uint16 panid;
        uint8 st[5];
        zb_net_get_status(&state, &channel, &panid, &joined_n);
        st[0] = state; st[1] = channel;
        st[2] = (uint8)(panid & 0xFF); st[3] = (uint8)(panid >> 8);
        st[4] = joined_n;
        (void)GwCoord_SendFrame(CMD_ZB_NET_STATUS, st, 5);
    }
}

static void do_cmd(const frame_t *f)
{
    /* ZB_CMD payload 布局同 ZB_REPORT：u16 nodeid; u8 ep; u16 cluster; u8 dtype; u8 vlen; val[]. */
    uint16 nodeid;
    uint8  ep;
    uint16 cluster;
    uint8  dtype;
    uint8  vlen;
    const uint8 *val;

    if (f->plen < 7) { GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_CMD); return; }
    nodeid  = (uint16)f->payload[0] | ((uint16)f->payload[1] << 8);
    ep      = f->payload[2];
    cluster = (uint16)f->payload[3] | ((uint16)f->payload[4] << 8);
    dtype   = f->payload[5];
    vlen    = f->payload[6];
    val     = &f->payload[7];
    if ((uint16)(7 + vlen) > f->plen) { GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_CMD); return; }

    if (cluster == ZCL_CLU_ONOFF && dtype == DTYPE_BOOL && vlen == 1) {
        if (zb_net_send_onoff(nodeid, ep, val[0]) != 0) {
            GwCoord_SendErr(ERRC_SEND_FAIL, CMD_ZB_CMD);
        }
    } else {
        /* 其他 cluster 暂不支持，未来在此扩展 */
        GwCoord_SendErr(ERRC_UNKNOWN, CMD_ZB_CMD);
    }
}

void cmd_map_dispatch(const frame_t *f)
{
    switch (f->cmd) {
    case CMD_ZB_PING:       do_ping(f);        break;
    case CMD_ZB_ALLOW_JOIN: do_allow_join(f);  break;
    case CMD_ZB_LIST_NODES: do_list_nodes(f);  break;
    case CMD_ZB_CMD:        do_cmd(f);         break;
    default:
        GwCoord_SendErr(ERRC_UNKNOWN, f->cmd);
        break;
    }
}
