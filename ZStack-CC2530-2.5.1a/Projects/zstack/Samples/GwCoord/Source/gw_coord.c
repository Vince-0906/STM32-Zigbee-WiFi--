/*
 * GwCoord 主任务：AF 端点注册、事件处理、ZDO 状态变更、ZCL Report 转 ZB_REPORT。
 * 引用规范书 §4.5 / §5 / §6 与 CC2530.md §3/§5。
 */

#include "gw_coord.h"
#include "uart_link.h"
#include "cmd_map.h"
#include "zb_net.h"
#include "frame.h"

#include "OSAL.h"
#include "OSAL_Timers.h"
#include "ZComDef.h"
#include "AF.h"
#include "ZDApp.h"
#include "ZDProfile.h"
#include "ZGlobals.h"
#include "hal_led.h"

/* ---------- 变量 ---------- */
uint8 GwCoord_TaskID = 0xFF;

static endPointDesc_t s_epDesc;
static SimpleDescriptionFormat_t s_simpleDesc;
static cId_t XDATA_QUAL s_inClusters[GWCOORD_CLUSTERS_MAX] = {
    ZCL_CLU_TEMP, ZCL_CLU_HUM, ZCL_CLU_LUX, ZCL_CLU_ONOFF
};
static cId_t XDATA_QUAL s_outClusters[1] = { ZCL_CLU_ONOFF };

static uint8 s_tx_buf[FRAME_MAX];

/* ---------- 内部辅助 ---------- */
static void on_af_incoming(afIncomingMSGPacket_t *pkt);
static void on_zdo_state_change(uint8 state);

/* ---------- 对外 API ---------- */
int8 GwCoord_SendFrame(uint8 cmd, const uint8 *payload, uint8 plen)
{
    int16 n;
    n = frame_encode(cmd, payload, plen, s_tx_buf, sizeof(s_tx_buf));
    if (n <= 0) return (int8)n;
    return uart_link_tx(s_tx_buf, (uint16)n);
}

void GwCoord_SendErr(uint8 code, uint8 ctx)
{
    uint8 p[2];
    p[0] = code;
    p[1] = ctx;
    (void)GwCoord_SendFrame(CMD_ZB_ERR, p, 2);
}

void GwCoord_OnFrame(const frame_t *f)
{
    /* 数据灯脉冲（P1.1）交由 HAL_LED 抽象 */
    HalLedSet(HAL_LED_2, HAL_LED_MODE_BLINK);
    cmd_map_dispatch(f);
}

/* ---------- OSAL ---------- */
void GwCoord_Init(uint8 task_id)
{
    GwCoord_TaskID = task_id;

    /* 1) Simple Desc + 端点注册 */
    s_simpleDesc.EndPoint             = GWCOORD_ENDPOINT;
    s_simpleDesc.AppProfId            = GWCOORD_PROFILE_ID;
    s_simpleDesc.AppDeviceId          = GWCOORD_DEVICE_ID;
    s_simpleDesc.AppDevVer            = GWCOORD_DEVICE_VERSION;
    s_simpleDesc.Reserved             = 0;
    s_simpleDesc.AppNumInClusters     = GWCOORD_CLUSTERS_MAX;
    s_simpleDesc.pAppInClusterList    = s_inClusters;
    s_simpleDesc.AppNumOutClusters    = 1;
    s_simpleDesc.pAppOutClusterList   = s_outClusters;

    s_epDesc.endPoint    = GWCOORD_ENDPOINT;
    s_epDesc.task_id     = &GwCoord_TaskID;
    s_epDesc.simpleDesc  = &s_simpleDesc;
    s_epDesc.latencyReq  = noLatencyReqs;
    afRegister(&s_epDesc);

    /* 2) 订阅 ZDO 状态变更消息 */
    ZDO_RegisterForZDOMsg(GwCoord_TaskID, End_Device_annce);

    /* 3) 网络层 / UART 链路初始化 */
    zb_net_init();
    uart_link_init(GwCoord_TaskID);

    /* 4) 启动心跳计时器 */
    osal_start_reloadTimer(GwCoord_TaskID, GW_EVT_HEARTBEAT, 1000);

    /* LED 初始全灭 */
    HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
    HalLedSet(HAL_LED_2, HAL_LED_MODE_OFF);
}

UINT16 GwCoord_ProcessEvent(uint8 task_id, uint16 events)
{
    afIncomingMSGPacket_t *MSGpkt;

    (void)task_id;

    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(GwCoord_TaskID)) != 0) {
            switch (MSGpkt->hdr.event) {
            case ZDO_STATE_CHANGE:
                on_zdo_state_change((uint8)ZDApp_NwkState());
                break;
            case AF_INCOMING_MSG_CMD:
                on_af_incoming(MSGpkt);
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
        return (events ^ GW_EVT_HEARTBEAT);
    }

    if (events & GW_EVT_JOIN_TMO) {
        /* 预留：入网窗关闭通知 */
        return (events ^ GW_EVT_JOIN_TMO);
    }

    return 0;
}

/* ---------- 内部实现 ---------- */
static void on_zdo_state_change(uint8 state)
{
    uint16 panid   = _NIB.nwkPanId;
    uint8  channel = _NIB.nwkLogicalChannel;
    zb_net_on_state(state, panid, channel);

    if (state == DEV_ZB_COORD) {
        HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);  /* 入网成功 */
    }
}

/* 从 ZCL Report 提取 attrId=0x0000 的数值并上抛 ZB_REPORT。
 * ZCL frame(最小): [FC 1B][Seq 1B][Cmd 1B][AttrID lo][AttrID hi][DataType 1B][Value ...]
 * 只处理 Cmd = 0x0A Report Attributes（单属性一条） 与 0x01 Read Resp 结构。
 */
static void af_to_zb_report(afIncomingMSGPacket_t *pkt)
{
    uint8 *zcl = pkt->cmd.Data;
    uint8  zlen = pkt->cmd.DataLength;
    uint8  fc, cmd;
    uint16 attrId;
    uint8  dtype;
    uint8  vlen;
    const uint8 *val;
    uint8  out[FRAME_PAYLOAD_MAX];
    uint8 *p;

    if (zlen < 6) return;
    fc = zcl[0];
    /* [0]=FC [1]=Seq [2]=Cmd */
    cmd = zcl[2];

    if (cmd == 0x0A /* Report Attributes */) {
        attrId = (uint16)zcl[3] | ((uint16)zcl[4] << 8);
        dtype  = zcl[5];
        val    = &zcl[6];
        (void)attrId; (void)fc;

        /* 按 dtype 计算 vlen */
        switch (dtype) {
            case DTYPE_BOOL: case DTYPE_U8: case DTYPE_I8:   vlen = 1; break;
            case DTYPE_U16:  case DTYPE_I16:                  vlen = 2; break;
            case DTYPE_U32:  case DTYPE_I32:                  vlen = 4; break;
            default:                                           vlen = 0; break;
        }
        if (vlen == 0 || (uint16)(6 + vlen) > zlen) return;

        p = out;
        *p++ = (uint8)(pkt->srcAddr.addr.shortAddr & 0xFF);
        *p++ = (uint8)(pkt->srcAddr.addr.shortAddr >> 8);
        *p++ = pkt->srcAddr.endPoint;
        *p++ = (uint8)(pkt->clusterId & 0xFF);
        *p++ = (uint8)(pkt->clusterId >> 8);
        *p++ = dtype;
        *p++ = vlen;
        osal_memcpy(p, val, vlen); p += vlen;
        (void)GwCoord_SendFrame(CMD_ZB_REPORT, out, (uint8)(p - out));

        /* 触达节点表 */
        (void)zb_net_touch(pkt->srcAddr.addr.shortAddr, 0, 0, (int8)pkt->rssi);
    }
}

static void on_af_incoming(afIncomingMSGPacket_t *pkt)
{
    /* 数据灯短亮 */
    HalLedSet(HAL_LED_2, HAL_LED_MODE_BLINK);
    af_to_zb_report(pkt);
}
