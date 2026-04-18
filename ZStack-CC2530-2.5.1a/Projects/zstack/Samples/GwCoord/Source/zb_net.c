/*
 * Coordinator-side Zigbee node table and network state.
 */

#include "zb_net.h"
#include "gw_coord.h"
#include "nv_store.h"

#include "OSAL.h"
#include "OSAL_Timers.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"
#include "AF.h"
#include "nwk_util.h"
#include "ZGlobals.h"

static zb_node_t XDATA_QUAL s_nodes[ZB_NODE_MAX];
static uint8 s_node_cnt = 0;

static uint8  s_dev_state = 0;
static uint16 s_panid = 0xFFFF;
static uint8  s_channel = ZB_CHANNEL_DEFAULT;
static uint8  s_allow_join_rem_s = 0;
static uint32 s_last_tick_ms = 0;

static zb_node_t *find_slot(uint16 nodeid)
{
    uint8 i;

    for (i = 0; i < s_node_cnt; ++i) {
        if (s_nodes[i].nodeid == nodeid) {
            return &s_nodes[i];
        }
    }
    return 0;
}

static zb_node_t *alloc_slot(void)
{
    if (s_node_cnt >= ZB_NODE_MAX) {
        uint8 i;
        uint8 oldest = 0;
        uint32 oldest_ms = s_nodes[0].last_seen_ms;

        for (i = 1; i < ZB_NODE_MAX; ++i) {
            if (s_nodes[i].last_seen_ms < oldest_ms) {
                oldest_ms = s_nodes[i].last_seen_ms;
                oldest = i;
            }
        }
        osal_memset(&s_nodes[oldest], 0, sizeof(zb_node_t));
        return &s_nodes[oldest];
    }

    osal_memset(&s_nodes[s_node_cnt], 0, sizeof(zb_node_t));
    return &s_nodes[s_node_cnt++];
}

void zb_net_init(void)
{
    uint16 nv_pan = 0xFFFF;
    uint8 nv_ch = ZB_CHANNEL_DEFAULT;

    osal_memset(s_nodes, 0, sizeof(s_nodes));
    s_node_cnt = 0;

    if (nv_load_u16(NV_ID_PANID, &nv_pan) == 0 && nv_pan != 0xFFFF) {
        s_panid = nv_pan;
        ZDO_Config_Node_Descriptor.LogicalType = NODETYPE_COORDINATOR;
    }
    if (nv_load_u8(NV_ID_CHANNEL, &nv_ch) == 0 && nv_ch != 0xFF) {
        s_channel = nv_ch;
    }
    s_last_tick_ms = osal_GetSystemClock();
}

void zb_net_tick(uint32 now_ms)
{
    uint32 delta;
    uint8 i;

    if (now_ms - s_last_tick_ms < 1000u) {
        return;
    }
    delta = (now_ms - s_last_tick_ms) / 1000u;
    s_last_tick_ms = now_ms;

    if (s_allow_join_rem_s != 0xFFu && s_allow_join_rem_s > 0u) {
        if (delta >= s_allow_join_rem_s) {
            s_allow_join_rem_s = 0;
            NLME_PermitJoiningRequest(0);
        } else {
            s_allow_join_rem_s = (uint8)(s_allow_join_rem_s - delta);
        }
    }

    for (i = 0; i < s_node_cnt; ++i) {
        if ((s_nodes[i].flags & 0x01u) &&
            (now_ms - s_nodes[i].last_seen_ms) > (uint32)(3u * ZB_HEARTBEAT_S * 1000u)) {
            zb_net_mark_offline(s_nodes[i].nodeid);
        }
    }
}

void zb_net_allow_join(uint8 sec)
{
    if (sec == 0u) {
        NLME_PermitJoiningRequest(0);
        s_allow_join_rem_s = 0;
    } else if (sec == 0xFFu) {
        NLME_PermitJoiningRequest(0xFFu);
        s_allow_join_rem_s = 0xFFu;
    } else {
        NLME_PermitJoiningRequest(sec);
        s_allow_join_rem_s = sec;
    }
}

zb_node_t *zb_net_find(uint16 nodeid)
{
    return find_slot(nodeid);
}

zb_node_t *zb_net_touch(uint16 nodeid, const uint8 *ieee, uint8 type, int8 rssi)
{
    zb_node_t *n;

    n = find_slot(nodeid);
    if (n == 0) {
        n = alloc_slot();
    }
    if (n == 0) {
        return 0;
    }

    n->nodeid = nodeid;
    if (ieee != 0) {
        osal_memcpy(n->ieee, ieee, 8);
    }
    if (type != ZB_DEV_UNKNOWN) {
        n->type = type;
    }
    n->rssi = rssi;
    n->last_seen_ms = osal_GetSystemClock();
    n->flags |= 0x01u;
    n->miss_cnt = 0;
    return n;
}

const zb_node_t *zb_net_find_by_role(uint8 role)
{
    uint8 i;

    for (i = 0; i < s_node_cnt; ++i) {
        if (s_nodes[i].role == role) {
            return &s_nodes[i];
        }
    }
    return 0;
}

uint8 zb_net_count(void)
{
    return s_node_cnt;
}

uint8 zb_net_online_count(void)
{
    uint8 i;
    uint8 cnt = 0;

    for (i = 0; i < s_node_cnt; ++i) {
        if (s_nodes[i].flags & 0x01u) {
            cnt++;
        }
    }
    return cnt;
}

const zb_node_t *zb_net_at(uint8 idx)
{
    if (idx >= s_node_cnt) {
        return 0;
    }
    return &s_nodes[idx];
}

void zb_net_mark_offline(uint16 nodeid)
{
    zb_node_t *n;

    n = find_slot(nodeid);
    if (n == 0) {
        return;
    }
    if ((n->flags & 0x01u) == 0u) {
        return;
    }

    n->flags &= (uint8)~0x01u;
    GwCoord_SendNodeInfo(n);
    GwCoord_SendNetStatus();
}

int8 zb_net_send_onoff(uint16 nodeid, uint8 ep, uint8 onoff)
{
    afAddrType_t dst;
    uint8 zcl_buf[3];
    static uint8 s_zcl_seq = 0;
    uint8 rc;
    endPointDesc_t *ep_desc;

    osal_memset(&dst, 0, sizeof(dst));
    dst.addrMode = (afAddrMode_t)Addr16Bit;
    dst.addr.shortAddr = nodeid;
    dst.endPoint = ep;

    zcl_buf[0] = 0x01;
    zcl_buf[1] = ++s_zcl_seq;
    zcl_buf[2] = onoff ? 0x01 : 0x00;

    ep_desc = afFindEndPointDesc(GWCOORD_ENDPOINT);
    if (ep_desc == 0) {
        return (int8)RC_ERR_PARAM;
    }

    rc = AF_DataRequest(&dst, ep_desc, ZCL_CLU_ONOFF,
                        sizeof(zcl_buf), zcl_buf,
                        &s_zcl_seq, AF_DISCV_ROUTE, AF_DEFAULT_RADIUS);
    return (rc == afStatus_SUCCESS) ? 0 : (int8)-1;
}

void zb_net_on_state(uint8 state, uint16 panid, uint8 channel)
{
    s_dev_state = state;
    s_panid = panid;
    s_channel = channel;
    GwCoord_SendNetStatus();

    if (state == DEV_ZB_COORD) {
        (void)nv_save_u16(NV_ID_PANID, panid);
        (void)nv_save_u8(NV_ID_CHANNEL, channel);
    }
}

void zb_net_get_status(uint8 *state, uint8 *channel, uint16 *panid, uint8 *joined_n)
{
    if (state) {
        *state = s_dev_state;
    }
    if (channel) {
        *channel = s_channel;
    }
    if (panid) {
        *panid = s_panid;
    }
    if (joined_n) {
        *joined_n = zb_net_online_count();
    }
}
