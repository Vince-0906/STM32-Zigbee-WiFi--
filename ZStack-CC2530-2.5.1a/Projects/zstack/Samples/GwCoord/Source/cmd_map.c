/*
 * Dispatch inbound UART commands from STM32 to coordinator services.
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
    if (f->plen < 1u) {
        GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_ALLOW_JOIN);
        return;
    }
    zb_net_allow_join(f->payload[0]);
    GwCoord_SendNetStatus();
}

static void do_list_nodes(const frame_t *f)
{
    uint8 i;
    uint8 cnt;
    const zb_node_t *n;

    (void)f;
    cnt = zb_net_count();
    for (i = 0; i < cnt; ++i) {
        n = zb_net_at(i);
        if (n != 0) {
            GwCoord_SendNodeInfo(n);
        }
    }
    if (cnt == 0u) {
        GwCoord_SendNetStatus();
    }
}

static void do_cmd(const frame_t *f)
{
    uint16 nodeid;
    uint8 ep;
    uint16 cluster;
    uint8 dtype;
    uint8 vlen;
    const uint8 *val;

    if (f->plen < 7u) {
        GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_CMD);
        return;
    }

    nodeid  = (uint16)f->payload[0] | ((uint16)f->payload[1] << 8);
    ep      = f->payload[2];
    cluster = (uint16)f->payload[3] | ((uint16)f->payload[4] << 8);
    dtype   = f->payload[5];
    vlen    = f->payload[6];
    val     = &f->payload[7];
    if ((uint16)(7u + vlen) > f->plen) {
        GwCoord_SendErr(ERRC_BAD_FRAME, CMD_ZB_CMD);
        return;
    }

    if (cluster == ZCL_CLU_ONOFF && dtype == DTYPE_BOOL && vlen == 1u) {
        if (zb_net_send_onoff(nodeid, ep, val[0]) != 0) {
            GwCoord_SendErr(ERRC_SEND_FAIL, CMD_ZB_CMD);
        }
    } else {
        GwCoord_SendErr(ERRC_UNKNOWN, CMD_ZB_CMD);
    }
}

void cmd_map_dispatch(const frame_t *f)
{
    switch (f->cmd) {
    case CMD_ZB_PING:
        do_ping(f);
        break;
    case CMD_ZB_ALLOW_JOIN:
        do_allow_join(f);
        break;
    case CMD_ZB_LIST_NODES:
        do_list_nodes(f);
        break;
    case CMD_ZB_CMD:
        do_cmd(f);
        break;
    default:
        GwCoord_SendErr(ERRC_UNKNOWN, f->cmd);
        break;
    }
}
