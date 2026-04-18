/*
 * GwCoord task: coordinator endpoint, ZDO callbacks, AF report translation
 * and local OLED refresh.
 */

#include "gw_coord.h"
#include "uart_link.h"
#include "cmd_map.h"
#include "zb_net.h"
#include "frame.h"
#include "gw_oled_view.h"

#include "OSAL.h"
#include "OSAL_Timers.h"
#include "ZComDef.h"
#include "AF.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"
#include "ZGlobals.h"
#include "hal_led.h"

uint8 GwCoord_TaskID = 0xFF;

static endPointDesc_t s_epDesc;
static SimpleDescriptionFormat_t s_simpleDesc;
static cId_t XDATA_QUAL s_inClusters[GWCOORD_CLUSTERS_MAX] = {
    ZCL_CLU_TEMP, ZCL_CLU_HUM, ZCL_CLU_LUX, ZCL_CLU_ONOFF
};
static cId_t XDATA_QUAL s_outClusters[1] = { ZCL_CLU_ONOFF };
static uint8 s_tx_buf[FRAME_MAX];

static void on_af_incoming(afIncomingMSGPacket_t *pkt);
static void on_af_data_confirm(afDataConfirm_t *cfm);
static void on_zdo_state_change(uint8 state);
static void on_zdo_cb_msg(zdoIncomingMsg_t *inMsg);
static void af_to_zb_report(afIncomingMSGPacket_t *pkt);
static uint8 role_from_cluster(uint16 cluster);
static uint8 device_type_from_capabilities(uint8 cap);

int8 GwCoord_SendFrame(uint8 cmd, const uint8 *payload, uint8 plen)
{
    int16 n;

    n = frame_encode(cmd, payload, plen, s_tx_buf, sizeof(s_tx_buf));
    if (n <= 0) {
        return (int8)n;
    }
    return uart_link_tx(s_tx_buf, (uint16)n);
}

void GwCoord_SendErr(uint8 code, uint8 ctx)
{
    uint8 p[2];

    p[0] = code;
    p[1] = ctx;
    (void)GwCoord_SendFrame(CMD_ZB_ERR, p, 2);
}

void GwCoord_SendNodeInfo(const zb_node_t *node)
{
    uint8 buf[7];
    uint32 diff_ms;

    if (node == 0) {
        return;
    }

    buf[0] = (uint8)(node->nodeid & 0xFFu);
    buf[1] = (uint8)(node->nodeid >> 8);
    buf[2] = node->type;
    buf[3] = (uint8)node->rssi;
    diff_ms = osal_GetSystemClock() - node->last_seen_ms;
    buf[4] = (diff_ms > 255000u) ? 0xFFu : (uint8)(diff_ms / 1000u);
    buf[5] = node->role;
    buf[6] = (node->flags & 0x01u) ? 1u : 0u;
    (void)GwCoord_SendFrame(CMD_ZB_NODE_INFO, buf, sizeof(buf));
}

void GwCoord_SendNetStatus(void)
{
    uint8 payload[5];
    uint8 state;
    uint8 channel;
    uint16 panid;
    uint8 joined_n;

    zb_net_get_status(&state, &channel, &panid, &joined_n);
    payload[0] = state;
    payload[1] = channel;
    payload[2] = (uint8)(panid & 0xFFu);
    payload[3] = (uint8)(panid >> 8);
    payload[4] = joined_n;
    (void)GwCoord_SendFrame(CMD_ZB_NET_STATUS, payload, sizeof(payload));
}

void GwCoord_RefreshLocalDisplay(void)
{
    gw_oled_view_refresh();
}

void GwCoord_OnFrame(const frame_t *f)
{
    HalLedSet(HAL_LED_2, HAL_LED_MODE_BLINK);
    cmd_map_dispatch(f);
}

void GwCoord_Init(uint8 task_id)
{
    GwCoord_TaskID = task_id;

    s_simpleDesc.EndPoint           = GWCOORD_ENDPOINT;
    s_simpleDesc.AppProfId          = GWCOORD_PROFILE_ID;
    s_simpleDesc.AppDeviceId        = GWCOORD_DEVICE_ID;
    s_simpleDesc.AppDevVer          = GWCOORD_DEVICE_VERSION;
    s_simpleDesc.Reserved           = 0;
    s_simpleDesc.AppNumInClusters   = GWCOORD_CLUSTERS_MAX;
    s_simpleDesc.pAppInClusterList  = s_inClusters;
    s_simpleDesc.AppNumOutClusters  = 1;
    s_simpleDesc.pAppOutClusterList = s_outClusters;

    s_epDesc.endPoint   = GWCOORD_ENDPOINT;
    s_epDesc.task_id    = &GwCoord_TaskID;
    s_epDesc.simpleDesc = &s_simpleDesc;
    s_epDesc.latencyReq = noLatencyReqs;
    afRegister(&s_epDesc);

    ZDO_RegisterForZDOMsg(GwCoord_TaskID, Device_annce);

    zb_net_init();
    uart_link_init(GwCoord_TaskID);
    gw_oled_view_init();

    (void)osal_start_reload_timer(GwCoord_TaskID, GW_EVT_HEARTBEAT, 1000);

    HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
    HalLedSet(HAL_LED_2, HAL_LED_MODE_OFF);
    GwCoord_RefreshLocalDisplay();
}

UINT16 GwCoord_ProcessEvent(uint8 task_id, uint16 events)
{
    afIncomingMSGPacket_t *MSGpkt;

    (void)task_id;

    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(GwCoord_TaskID)) != 0) {
            switch (MSGpkt->hdr.event) {
            case ZDO_STATE_CHANGE:
                on_zdo_state_change((uint8)devState);
                break;

            case ZDO_CB_MSG:
                on_zdo_cb_msg((zdoIncomingMsg_t *)MSGpkt);
                break;

            case AF_INCOMING_MSG_CMD:
                on_af_incoming(MSGpkt);
                break;

            case AF_DATA_CONFIRM_CMD:
                on_af_data_confirm((afDataConfirm_t *)MSGpkt);
                break;

            default:
                break;
            }
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & GW_EVT_UART_RX) {
        uart_link_drain();
        return (events ^ GW_EVT_UART_RX);
    }

    if (events & GW_EVT_HEARTBEAT) {
        zb_net_tick(osal_GetSystemClock());
        GwCoord_RefreshLocalDisplay();
        return (events ^ GW_EVT_HEARTBEAT);
    }

    if (events & GW_EVT_JOIN_TMO) {
        return (events ^ GW_EVT_JOIN_TMO);
    }

    return 0;
}

static void on_zdo_state_change(uint8 state)
{
    uint16 panid;
    uint8 channel;

    panid = _NIB.nwkPanId;
    channel = _NIB.nwkLogicalChannel;
    zb_net_on_state(state, panid, channel);

    if (state == DEV_ZB_COORD) {
        HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);
    }
    GwCoord_RefreshLocalDisplay();
}

static void on_zdo_cb_msg(zdoIncomingMsg_t *inMsg)
{
    if (inMsg->clusterID == Device_annce) {
        ZDO_DeviceAnnce_t annce;
        zb_node_t *node;

        ZDO_ParseDeviceAnnce(inMsg, &annce);
        node = zb_net_touch(annce.nwkAddr, annce.extAddr,
                            device_type_from_capabilities(annce.capabilities), 0);
        if (node != 0) {
            GwCoord_SendNodeInfo(node);
            GwCoord_SendNetStatus();
            GwCoord_RefreshLocalDisplay();
        }
    }
}

static void on_af_data_confirm(afDataConfirm_t *cfm)
{
    if (cfm->hdr.status != ZSuccess) {
        GwCoord_SendErr(ERRC_SEND_FAIL, cfm->transID);
    }
}

static void af_to_zb_report(afIncomingMSGPacket_t *pkt)
{
    uint8 *zcl;
    uint8 zlen;
    uint8 cmd;
    uint8 dtype;
    uint8 vlen;
    const uint8 *val;
    uint8 out[FRAME_PAYLOAD_MAX];
    uint8 *p;
    zb_node_t *node;
    zb_node_t *existing;
    uint8 role;
    uint8 role_changed = 0;
    uint8 online_changed = 0;

    zcl = pkt->cmd.Data;
    zlen = pkt->cmd.DataLength;
    if (zlen < 6u) {
        return;
    }

    cmd = zcl[2];
    if (cmd != 0x0Au) {
        return;
    }

    dtype = zcl[5];
    val = &zcl[6];

    switch (dtype) {
    case DTYPE_BOOL:
    case DTYPE_U8:
    case DTYPE_I8:
        vlen = 1u;
        break;
    case DTYPE_U16:
    case DTYPE_I16:
        vlen = 2u;
        break;
    case DTYPE_U32:
    case DTYPE_I32:
        vlen = 4u;
        break;
    default:
        return;
    }
    if ((uint16)(6u + vlen) > zlen) {
        return;
    }

    existing = zb_net_find(pkt->srcAddr.addr.shortAddr);
    if (existing == 0 || (existing->flags & 0x01u) == 0u) {
        online_changed = 1u;
    }
    node = zb_net_touch(pkt->srcAddr.addr.shortAddr, 0, ZB_DEV_ENDDEV, (int8)pkt->rssi);
    if (node == 0) {
        return;
    }

    role = role_from_cluster(pkt->clusterId);
    if (role != ZB_ROLE_UNKNOWN && node->role != role) {
        node->role = role;
        role_changed = 1u;
    }

    if (pkt->clusterId == ZCL_CLU_TEMP && dtype == DTYPE_I16) {
        node->temp_x100 = (int16)((uint16)val[0] | ((uint16)val[1] << 8));
    } else if (pkt->clusterId == ZCL_CLU_HUM && dtype == DTYPE_U16) {
        node->hum_x100 = (uint16)val[0] | ((uint16)val[1] << 8);
    } else if (pkt->clusterId == ZCL_CLU_LUX && dtype == DTYPE_U16) {
        node->lux = (uint16)val[0] | ((uint16)val[1] << 8);
    } else if (pkt->clusterId == ZCL_CLU_ONOFF && dtype == DTYPE_BOOL) {
        if (pkt->srcAddr.endPoint == 1u) {
            node->led_state = val[0] ? 1u : 0u;
        } else if (pkt->srcAddr.endPoint == 2u) {
            node->buzzer_state = val[0] ? 1u : 0u;
        }
    }

    p = out;
    *p++ = (uint8)(pkt->srcAddr.addr.shortAddr & 0xFFu);
    *p++ = (uint8)(pkt->srcAddr.addr.shortAddr >> 8);
    *p++ = pkt->srcAddr.endPoint;
    *p++ = (uint8)(pkt->clusterId & 0xFFu);
    *p++ = (uint8)(pkt->clusterId >> 8);
    *p++ = dtype;
    *p++ = vlen;
    osal_memcpy(p, val, vlen);
    p += vlen;

    (void)GwCoord_SendFrame(CMD_ZB_REPORT, out, (uint8)(p - out));
    if (role_changed || online_changed) {
        GwCoord_SendNodeInfo(node);
    }
    GwCoord_RefreshLocalDisplay();
}

static void on_af_incoming(afIncomingMSGPacket_t *pkt)
{
    HalLedSet(HAL_LED_2, HAL_LED_MODE_BLINK);
    af_to_zb_report(pkt);
}

static uint8 role_from_cluster(uint16 cluster)
{
    if (cluster == ZCL_CLU_TEMP || cluster == ZCL_CLU_HUM) {
        return ZB_ROLE_TEMP_HUM;
    }
    if (cluster == ZCL_CLU_LUX) {
        return ZB_ROLE_LUX;
    }
    return ZB_ROLE_UNKNOWN;
}

static uint8 device_type_from_capabilities(uint8 cap)
{
    return (cap & CAPINFO_DEVICETYPE_FFD) ? ZB_DEV_ROUTER : ZB_DEV_ENDDEV;
}
