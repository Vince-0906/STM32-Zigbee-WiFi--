/*
 * ZbNode task: AF endpoint registration, periodic sensor reporting, and
 * incoming OnOff command handling.
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

typedef struct {
    int8  last_result;
    uint8 last_status;
    uint8 last_transid;
    uint8 pending_reports;
    uint8 fail_count;
    uint8 consecutive_fail;
} report_diag_t;

static endPointDesc_t s_ep1, s_ep2;
static SimpleDescriptionFormat_t s_sd1, s_sd2;
static cId_t s_in_sensor[1] = { 0x0006u };
#if NODE_ROLE == 1
static cId_t s_out_sensor[3] = { 0x0402u, 0x0405u, 0x0006u };
#else
static cId_t s_out_sensor[2] = { 0x0400u, 0x0006u };
#endif
static cId_t s_in_buzzer[1] = { 0x0006u };
static cId_t s_out_buzzer[1] = { 0x0006u };
static uint8 s_joined = 0;
static devStates_t s_nwk_state = DEV_INIT;
static report_diag_t s_report_diag;

#define CLU_ONOFF         0x0006u
#define CLU_TEMP          0x0402u
#define CLU_HUM           0x0405u
#define CLU_LUX           0x0400u
#define REPORT_RETRY_MS   200u

static uint8 network_ready(void)
{
    return s_joined &&
           ((s_nwk_state == DEV_END_DEVICE) ||
            (s_nwk_state == DEV_ROUTER) ||
            (s_nwk_state == DEV_ZB_COORD));
}

static void note_local_report_failure(int8 rc)
{
    s_report_diag.last_result = rc;
    s_report_diag.fail_count++;
    s_report_diag.consecutive_fail++;

    if (network_ready()) {
        (void)osal_start_timerEx(ZbNode_TaskID, ZN_EVT_REPORT_RETRY, REPORT_RETRY_MS);
    }
}

static int8 send_attr_report(uint8 src_ep, uint16 cluster, uint16 attrId,
                             uint8 dtype, const void *val, uint8 vlen)
{
    int8 rc;

    rc = zcl_report_send(src_ep, cluster, attrId, dtype, val, vlen);
    s_report_diag.last_result = rc;
    if (rc == 0) {
        s_report_diag.pending_reports++;
    } else {
        note_local_report_failure(rc);
    }
    return rc;
}

static void register_endpoints(void)
{
    s_sd1.EndPoint           = ZBNODE_EP_SENSOR;
    s_sd1.AppProfId          = ZBNODE_PROFILE_ID;
    s_sd1.AppDeviceId        = ZBNODE_DEVICE_ID;
    s_sd1.AppDevVer          = ZBNODE_DEVICE_VER;
    s_sd1.Reserved           = 0;
    s_sd1.AppNumInClusters   = sizeof(s_in_sensor) / sizeof(s_in_sensor[0]);
    s_sd1.pAppInClusterList  = s_in_sensor;
    s_sd1.AppNumOutClusters  = sizeof(s_out_sensor) / sizeof(s_out_sensor[0]);
    s_sd1.pAppOutClusterList = s_out_sensor;

    s_ep1.endPoint   = ZBNODE_EP_SENSOR;
    s_ep1.task_id    = &ZbNode_TaskID;
    s_ep1.simpleDesc = &s_sd1;
    s_ep1.latencyReq = noLatencyReqs;
    afRegister(&s_ep1);

    s_sd2.EndPoint           = ZBNODE_EP_BUZZER;
    s_sd2.AppProfId          = ZBNODE_PROFILE_ID;
    s_sd2.AppDeviceId        = ZBNODE_DEVICE_ID;
    s_sd2.AppDevVer          = ZBNODE_DEVICE_VER;
    s_sd2.Reserved           = 0;
    s_sd2.AppNumInClusters   = sizeof(s_in_buzzer) / sizeof(s_in_buzzer[0]);
    s_sd2.pAppInClusterList  = s_in_buzzer;
    s_sd2.AppNumOutClusters  = sizeof(s_out_buzzer) / sizeof(s_out_buzzer[0]);
    s_sd2.pAppOutClusterList = s_out_buzzer;

    s_ep2.endPoint   = ZBNODE_EP_BUZZER;
    s_ep2.task_id    = &ZbNode_TaskID;
    s_ep2.simpleDesc = &s_sd2;
    s_ep2.latencyReq = noLatencyReqs;
    afRegister(&s_ep2);
}

void ZbNode_Init(uint8 task_id)
{
    ZbNode_TaskID = task_id;
    s_joined = 0;
    s_nwk_state = DEV_INIT;
    osal_memset(&s_report_diag, 0, sizeof(s_report_diag));

    actuator_init();
#if NODE_ROLE == 1
    dht11_init();
#else
    ldr_init();
#endif
    register_endpoints();

    (void)osal_start_reload_timer(ZbNode_TaskID, ZN_EVT_REPORT, NODE_REPORT_PERIOD_MS);
}

static void do_report(void)
{
    if (!network_ready()) {
        return;
    }

#if NODE_ROLE == 1
    {
        int16 t = 0;
        uint16 h = 0;

        if (dht11_read(&t, &h) == 0) {
            (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_TEMP, 0x0000, 0x10, &t, 2);
            (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_HUM,  0x0000, 0x11, &h, 2);
        } else {
            note_local_report_failure(-7);
        }
    }
#else
    {
        uint16 lux = ldr_read_lux();

        (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_LUX, 0x0000, 0x11, &lux, 2);
    }
#endif
}

static void on_incoming(afIncomingMSGPacket_t *pkt)
{
    uint8 *zcl;
    uint8 zlen;
    uint8 cmd;
    uint8 seq;
    uint8 onoff;
    uint8 vstate;

    if (pkt->clusterId != CLU_ONOFF) {
        return;
    }

    zcl = pkt->cmd.Data;
    zlen = pkt->cmd.DataLength;
    if (zlen < 3) {
        return;
    }

    seq = zcl[1];
    cmd = zcl[2];
    switch (cmd) {
    case 0x00:
        onoff = 0;
        break;
    case 0x01:
        onoff = 1;
        break;
    case 0x02:
        if (pkt->endPoint == ZBNODE_EP_SENSOR) {
            onoff = (uint8)(!actuator_led_state());
        } else {
            onoff = (uint8)(!actuator_buzzer_state());
        }
        break;
    default:
        return;
    }

    if (pkt->endPoint == ZBNODE_EP_SENSOR) {
        actuator_led_set(onoff);
    } else if (pkt->endPoint == ZBNODE_EP_BUZZER) {
        actuator_buzzer_set(onoff);
    }

    (void)zcl_default_response(pkt->endPoint, CLU_ONOFF, seq, cmd, 0x00);
    vstate = onoff;
    (void)send_attr_report(pkt->endPoint, CLU_ONOFF, 0x0000, 0x01, &vstate, 1);
}

static void on_data_confirm(afDataConfirm_t *cfm)
{
    if (s_report_diag.pending_reports > 0) {
        s_report_diag.pending_reports--;
    }

    s_report_diag.last_status = cfm->hdr.status;
    s_report_diag.last_transid = cfm->transID;
    if (cfm->hdr.status == ZSuccess) {
        s_report_diag.last_result = 0;
        s_report_diag.consecutive_fail = 0;
    } else {
        note_local_report_failure((int8)cfm->hdr.status);
    }
}

UINT16 ZbNode_ProcessEvent(uint8 task_id, uint16 events)
{
    afIncomingMSGPacket_t *MSGpkt;

    (void)task_id;

    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(ZbNode_TaskID)) != 0) {
            switch (MSGpkt->hdr.event) {
            case AF_INCOMING_MSG_CMD:
                on_incoming(MSGpkt);
                break;

            case AF_DATA_CONFIRM_CMD:
                on_data_confirm((afDataConfirm_t *)MSGpkt);
                break;

            case ZDO_STATE_CHANGE:
                s_nwk_state = (devStates_t)MSGpkt->hdr.status;
                if ((s_nwk_state == DEV_END_DEVICE) ||
                    (s_nwk_state == DEV_ROUTER) ||
                    (s_nwk_state == DEV_ZB_COORD)) {
                    if (!s_joined) {
                        s_joined = 1;
                        do_report();
                    }
                } else {
                    s_joined = 0;
                }
                break;

            default:
                break;
            }
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & ZN_EVT_REPORT) {
        do_report();
        return (events ^ ZN_EVT_REPORT);
    }

    if (events & ZN_EVT_REPORT_RETRY) {
        do_report();
        return (events ^ ZN_EVT_REPORT_RETRY);
    }

    return 0;
}
