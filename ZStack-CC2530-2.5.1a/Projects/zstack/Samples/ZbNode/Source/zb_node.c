/*
 * ZbNode task: AF endpoint registration, periodic sensor reporting, and
 * incoming OnOff command handling.
 */

#include "zb_node.h"
#include "actuator.h"
#include "zcl_report.h"
#include "node_oled_view.h"
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
#include "ZGlobals.h"
#include "ioCC2530.h"
#include "nwk.h"
#include "nwk_globals.h"

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
static uint8 s_nwk_joined = 0;
static uint8 s_link_ready = 0;
static uint8 s_wait_first_report_cfm = 0;
static devStates_t s_nwk_state = DEV_INIT;
static report_diag_t s_report_diag;
static uint8 s_status_led_phase = 0;
static uint8 s_startup_leds_owned = 1;

#define CLU_ONOFF         0x0006u
#define CLU_TEMP          0x0402u
#define CLU_HUM           0x0405u
#define CLU_LUX           0x0400u
#define REPORT_RETRY_MS   200u
#define STATUS_LED_TICK_MS 100u
#define STATUS_LED_SLOW_TICKS 10u
#define STATUS_LED_FAST_TICKS 4u

/* OLED 模型缓存：刷新由 ZN_EVT_STATUS_LED 滴答（100ms）触发 model_changed 才写屏。 */
static node_oled_model_t s_oled_model;

/* 前置声明：oled_publish() 引用了下方 network_ready()。 */
static uint8 network_joined(void);
static uint8 network_ready(void);

static void oled_publish(void)
{
    s_oled_model.online    = network_ready() ? 1u : 0u;
    s_oled_model.short_addr= NLME_GetShortAddr();
    s_oled_model.channel   = _NIB.nwkLogicalChannel;
    s_oled_model.led_on    = actuator_led_state();
    s_oled_model.buzzer_on = actuator_buzzer_state();
    node_oled_view_set(&s_oled_model);
}

static void indicator_pin_init(uint8 pin)
{
    P1SEL &= (uint8)~(1u << pin);
    P1DIR |= (uint8)(1u << pin);
}

static void indicator_pin_set(uint8 pin, uint8 on)
{
    uint8 mask = (uint8)(1u << pin);
    uint8 level = on ? ACT_ACTIVE_HIGH : (uint8)(!ACT_ACTIVE_HIGH);

    if (level) {
        P1 |= mask;
    } else {
        P1 &= (uint8)~mask;
    }
}

static void indicator_pin_init_all(void)
{
    indicator_pin_init(NODE_LED_PIN);
    indicator_pin_init(NODE_START_LED_PIN);
    indicator_pin_init(NODE_ALIVE_LED_PIN);
}

static void startup_led_set(uint8 on)
{
    indicator_pin_set(NODE_START_LED_PIN, on);
}

static void alive_led_set(uint8 on)
{
    indicator_pin_set(NODE_ALIVE_LED_PIN, on);
}

static void startup_pattern_set(uint8 on)
{
    indicator_pin_set(NODE_LED_PIN, on);
    startup_led_set(on);
}

static uint8 network_joined(void)
{
    return (s_nwk_state == DEV_END_DEVICE) ||
           (s_nwk_state == DEV_ROUTER) ||
           (s_nwk_state == DEV_ZB_COORD);
}

static uint8 network_joining(void)
{
    return (s_nwk_state == DEV_NWK_DISC) ||
           (s_nwk_state == DEV_NWK_JOINING) ||
           (s_nwk_state == DEV_NWK_REJOIN) ||
           (s_nwk_state == DEV_NWK_ORPHAN) ||
           (s_nwk_state == DEV_END_DEVICE_UNAUTH);
}

static void status_led_refresh(void)
{
    uint8 blink_on;

    alive_led_set(1u);

    if (s_link_ready) {
        if (s_startup_leds_owned) {
            startup_pattern_set(0u);
            actuator_led_set(0u);
            s_startup_leds_owned = 0u;
        } else {
            startup_led_set(0u);
        }
    } else if (network_joining() || network_joined()) {
        s_startup_leds_owned = 1u;
        blink_on = (uint8)((s_status_led_phase % STATUS_LED_FAST_TICKS) <
                           (STATUS_LED_FAST_TICKS / 2u));
        startup_pattern_set(blink_on);
    } else {
        s_startup_leds_owned = 1u;
        blink_on = (uint8)((s_status_led_phase % STATUS_LED_SLOW_TICKS) <
                           (STATUS_LED_SLOW_TICKS / 2u));
        startup_pattern_set(blink_on);
    }
}

void ZbNode_PreInit(void)
{
    indicator_pin_init_all();
    s_status_led_phase = 0;
    s_startup_leds_owned = 1u;
    s_nwk_joined = 0;
    s_link_ready = 0;
    s_wait_first_report_cfm = 0;
    s_nwk_state = DEV_INIT;
    status_led_refresh();
    /* 每次上电都强制清掉 NV 的网络 + 配置状态，让端节点以"首次加入"方式走发现 → 加入流程。
     * 只设 NETWORK_STATE 时，如果上次保留了 PANID/信道偏好，偶尔会卡在旧 PAN。
     * 同时清 CONFIG_STATE 可以连 TCLK / NIB 参数一起清零。 */
    (void)zgWriteStartupOptions(ZG_STARTUP_SET,
                                (uint8)(ZCD_STARTOPT_DEFAULT_NETWORK_STATE |
                                        ZCD_STARTOPT_DEFAULT_CONFIG_STATE));
}

static uint8 network_ready(void)
{
    return s_nwk_joined &&
           network_joined();
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
    s_nwk_joined = 0;
    s_link_ready = 0;
    s_wait_first_report_cfm = 0;
    s_nwk_state = DEV_INIT;
    s_status_led_phase = 0;
    s_startup_leds_owned = 1u;
    osal_memset(&s_report_diag, 0, sizeof(s_report_diag));

    actuator_init();
#if NODE_ROLE == 1
    dht11_init();
#else
    ldr_init();
#endif
    register_endpoints();

    /* OLED 视图：初始化（含 ~120ms 上电稳定延时）+ 渲染首帧 "尚未入网" 画面 */
    node_oled_view_init((uint8)NODE_ROLE);
    oled_publish();
    node_oled_view_refresh();

    (void)osal_start_reload_timer(ZbNode_TaskID, ZN_EVT_REPORT, NODE_REPORT_PERIOD_MS);
    (void)osal_start_reload_timer(ZbNode_TaskID, ZN_EVT_STATUS_LED, STATUS_LED_TICK_MS);
    status_led_refresh();
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
            s_oled_model.temp_x100 = t;
            s_oled_model.hum_x100  = h;
            s_oled_model.has_sample = 1u;
            (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_TEMP, 0x0000, 0x10, &t, 2);
            (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_HUM,  0x0000, 0x11, &h, 2);
        } else {
            note_local_report_failure(-7);
        }
    }
#else
    {
        uint16 lux = ldr_read_lux();

        s_oled_model.lux = lux;
        s_oled_model.has_sample = 1u;
        (void)send_attr_report(ZBNODE_EP_SENSOR, CLU_LUX, 0x0000, 0x11, &lux, 2);
    }
#endif
    oled_publish();
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
    oled_publish();
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
        if (s_wait_first_report_cfm && s_nwk_joined) {
            s_wait_first_report_cfm = 0u;
            s_link_ready = 1u;
            status_led_refresh();
        }
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
                if (network_joined()) {
                    if (!s_nwk_joined) {
                        s_nwk_joined = 1u;
                        s_link_ready = 0u;
                        s_wait_first_report_cfm = 1u;
                        do_report();
                    }
                } else {
                    s_nwk_joined = 0u;
                    s_link_ready = 0u;
                    s_wait_first_report_cfm = 0u;
                }
                status_led_refresh();
                oled_publish();
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

    if (events & ZN_EVT_STATUS_LED) {
        s_status_led_phase++;
        status_led_refresh();
        /* 100 ms 滴答顺便驱动 OLED：node_oled_view_refresh 内部 model_changed 后才真写屏，
         * 不变化时只是几次比较，开销可忽略。 */
        oled_publish();
        node_oled_view_refresh();
        return (events ^ ZN_EVT_STATUS_LED);
    }

    return 0;
}
