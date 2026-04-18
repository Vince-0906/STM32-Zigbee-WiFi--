/*
 * ZbNode 主任务：AF 端点注册、周期上报、入站 OnOff 命令处理。
 * NODE_ROLE=1 → 上报 DHT11 温湿度（ZCL 0x0402/0x0405）；
 * NODE_ROLE=2 → 上报光敏 lux（ZCL 0x0400）。
 * EP1 附带 OnOff 控制 LED；EP2 OnOff 控制 Buzzer（cluster 0x0006）。
 */

#include "zb_node.h"
#include "actuator.h"
#include "zcl_report.h"
#if NODE_ROLE == 1
#include "sensor_dht11.h"
#else
#include "sensor_ldr.h"
#endif

#include "OSAL.h"
#include "OSAL_Timers.h"
#include "ZComDef.h"
#include "ZDApp.h"
#include "AF.h"

uint8 ZbNode_TaskID = 0xFF;

static endPointDesc_t s_ep1, s_ep2;
static SimpleDescriptionFormat_t s_sd1, s_sd2;
static cId_t s_in1[3]  = { 0x0006u, 0x0402u, 0x0405u };   /* Node1 EP1 簇 */
static cId_t s_in1b[2] = { 0x0006u, 0x0400u };            /* Node2 EP1 簇 */
static cId_t s_in2[1]  = { 0x0006u };                     /* EP2 只有 OnOff（蜂鸣器） */

#define CLU_ONOFF   0x0006u
#define CLU_TEMP    0x0402u
#define CLU_HUM     0x0405u
#define CLU_LUX     0x0400u

static void register_endpoints(void)
{
    s_sd1.EndPoint          = ZBNODE_EP_SENSOR;
    s_sd1.AppProfId         = ZBNODE_PROFILE_ID;
    s_sd1.AppDeviceId       = ZBNODE_DEVICE_ID;
    s_sd1.AppDevVer         = ZBNODE_DEVICE_VER;
    s_sd1.Reserved          = 0;
#if NODE_ROLE == 1
    s_sd1.AppNumInClusters  = sizeof(s_in1) / sizeof(s_in1[0]);
    s_sd1.pAppInClusterList = s_in1;
#else
    s_sd1.AppNumInClusters  = sizeof(s_in1b) / sizeof(s_in1b[0]);
    s_sd1.pAppInClusterList = s_in1b;
#endif
    s_sd1.AppNumOutClusters = 0;
    s_sd1.pAppOutClusterList = 0;

    s_ep1.endPoint   = ZBNODE_EP_SENSOR;
    s_ep1.task_id    = &ZbNode_TaskID;
    s_ep1.simpleDesc = &s_sd1;
    s_ep1.latencyReq = noLatencyReqs;
    afRegister(&s_ep1);

    s_sd2.EndPoint          = ZBNODE_EP_BUZZER;
    s_sd2.AppProfId         = ZBNODE_PROFILE_ID;
    s_sd2.AppDeviceId       = ZBNODE_DEVICE_ID;
    s_sd2.AppDevVer         = ZBNODE_DEVICE_VER;
    s_sd2.Reserved          = 0;
    s_sd2.AppNumInClusters  = sizeof(s_in2) / sizeof(s_in2[0]);
    s_sd2.pAppInClusterList = s_in2;
    s_sd2.AppNumOutClusters = 0;
    s_sd2.pAppOutClusterList = 0;

    s_ep2.endPoint   = ZBNODE_EP_BUZZER;
    s_ep2.task_id    = &ZbNode_TaskID;
    s_ep2.simpleDesc = &s_sd2;
    s_ep2.latencyReq = noLatencyReqs;
    afRegister(&s_ep2);
}

void ZbNode_Init(uint8 task_id)
{
    ZbNode_TaskID = task_id;

    actuator_init();
#if NODE_ROLE == 1
    dht11_init();
#else
    ldr_init();
#endif
    register_endpoints();

    /* 周期上报定时器 */
    osal_start_reloadTimer(ZbNode_TaskID, ZN_EVT_REPORT, NODE_REPORT_PERIOD_MS);
}

static void do_report(void)
{
#if NODE_ROLE == 1
    int16 t = 0;
    uint16 h = 0;
    if (dht11_read(&t, &h) == 0) {
        (void)zcl_report_send(ZBNODE_EP_SENSOR, CLU_TEMP, 0x0000, 0x10 /* i16 */, &t, 2);
        (void)zcl_report_send(ZBNODE_EP_SENSOR, CLU_HUM,  0x0000, 0x11 /* u16 */, &h, 2);
    }
#else
    uint16 lux = ldr_read_lux();
    (void)zcl_report_send(ZBNODE_EP_SENSOR, CLU_LUX, 0x0000, 0x11 /* u16 */, &lux, 2);
#endif
}

static void on_incoming(afIncomingMSGPacket_t *pkt)
{
    /* 仅处理 OnOff cluster (0x0006)。ZCL 帧最小 3 字节：[FC][Seq][Cmd] */
    uint8 *zcl;
    uint8 zlen;
    uint8 cmd;
    uint8 seq;
    uint8 onoff;
    uint8 vstate;

    if (pkt->clusterId != CLU_ONOFF) return;
    zcl = pkt->cmd.Data;
    zlen = pkt->cmd.DataLength;
    if (zlen < 3) return;

    seq = zcl[1];
    cmd = zcl[2];
    switch (cmd) {
    case 0x00: onoff = 0; break;          /* Off */
    case 0x01: onoff = 1; break;          /* On */
    case 0x02:                            /* Toggle */
        if (pkt->endPoint == ZBNODE_EP_SENSOR) onoff = (uint8)(!actuator_led_state());
        else                                   onoff = (uint8)(!actuator_buzzer_state());
        break;
    default: return;
    }

    if (pkt->endPoint == ZBNODE_EP_SENSOR) {
        actuator_led_set(onoff);
    } else if (pkt->endPoint == ZBNODE_EP_BUZZER) {
        actuator_buzzer_set(onoff);
    }

    /* Default Response + 主动 Report OnOff（规范书 §6 "双重确认"） */
    (void)zcl_default_response(pkt->endPoint, CLU_ONOFF, seq, cmd, 0x00 /* SUCCESS */);
    vstate = onoff;
    (void)zcl_report_send(pkt->endPoint, CLU_ONOFF, 0x0000, 0x01 /* bool */, &vstate, 1);
}

UINT16 ZbNode_ProcessEvent(uint8 task_id, uint16 events)
{
    afIncomingMSGPacket_t *MSGpkt;
    (void)task_id;

    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(ZbNode_TaskID)) != 0) {
            if (MSGpkt->hdr.event == AF_INCOMING_MSG_CMD) {
                on_incoming(MSGpkt);
            }
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & ZN_EVT_REPORT) {
        do_report();
        return (events ^ ZN_EVT_REPORT);
    }

    return 0;
}
