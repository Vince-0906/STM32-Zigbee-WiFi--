/*
 * Router：ZB 帧 ↔ JSON 行 双向翻译，同时维护节点状态缓存。
 *
 * 约定：按加入顺序，第一个出现的节点短地址绑定为 Node1，第二个为 Node2。
 *       （可改为 NV 存储的固定映射，本版先自发现。）
 */

#include "router.h"
#include "automation.h"
#include "config.h"
#include "err.h"
#include "json.h"
#include "zb_link.h"
#include "wifi_link.h"
#include "thresholds.h"
#include "bsp_log.h"
#include "drv_tim.h"
#include <string.h>
#include <stdio.h>

static node_state_t s_nodes[ROUTER_NODE_MAX];
static uint16_t     s_idmap[ROUTER_NODE_MAX];
static uint8_t      s_idcnt = 0;

/* 固定映射：前两个节点 → N1 / N2 */
static uint16_t s_n1_id = 0;
static uint16_t s_n2_id = 0;

uint16_t router_node1_id(void) { return s_n1_id; }
uint16_t router_node2_id(void) { return s_n2_id; }

static node_state_t *slot_touch(uint16_t nodeid)
{
    uint8_t i;
    for (i = 0; i < s_idcnt; ++i) {
        if (s_idmap[i] == nodeid) return &s_nodes[i];
    }
    if (s_idcnt >= ROUTER_NODE_MAX) return 0;
    s_idmap[s_idcnt] = nodeid;
    memset(&s_nodes[s_idcnt], 0, sizeof(node_state_t));
    s_nodes[s_idcnt].online = 1;
    if (s_idcnt == 0) s_n1_id = nodeid;
    if (s_idcnt == 1) s_n2_id = nodeid;
    return &s_nodes[s_idcnt++];
}

const node_state_t *router_node(uint16_t nodeid)
{
    uint8_t i;
    for (i = 0; i < s_idcnt; ++i) {
        if (s_idmap[i] == nodeid) return &s_nodes[i];
    }
    return 0;
}

void router_init(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_idmap, 0, sizeof(s_idmap));
    s_idcnt = 0;
}

/* ---------- 上行：ZB → PC JSON ---------- */
static void emit_report_json(uint16_t nodeid, uint16_t cluster, node_state_t *ns)
{
    char buf[128];
    int n = json_begin(buf, sizeof(buf));
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "t", "report");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "node", nodeid);
    if (cluster == ZCL_CLU_TEMP || cluster == ZCL_CLU_HUM) {
        n = json_add_str (buf, sizeof(buf), (uint16_t)n, "kind", "temp_hum");
        n = json_add_fp2 (buf, sizeof(buf), (uint16_t)n, "temp", ns->temp_x100);
        n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "hum",  (uint32_t)(ns->hum_x100 / 100));
    } else if (cluster == ZCL_CLU_LUX) {
        n = json_add_str (buf, sizeof(buf), (uint16_t)n, "kind", "lux");
        n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "lux",  (uint32_t)ns->lux);
    } else {
        return;
    }
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) (void)wifi_link_send_line(buf, (uint16_t)n);
}

static void emit_status_json(uint16_t nodeid, node_state_t *ns)
{
    char buf[96];
    int n = json_begin(buf, sizeof(buf));
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "t", "status");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "node", nodeid);
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "led", ns->led_state ? "on" : "off");
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "buzzer", ns->buzzer_state ? "on" : "off");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) (void)wifi_link_send_line(buf, (uint16_t)n);
}

static void on_zb_report(const frame_t *f)
{
    uint16_t nodeid;
    uint8_t  ep;
    uint16_t cluster;
    uint8_t  dtype;
    uint8_t  vlen;
    const uint8_t *val;
    node_state_t *ns;

    if (f->plen < 7) return;
    nodeid  = (uint16_t)f->payload[0] | ((uint16_t)f->payload[1] << 8);
    ep      = f->payload[2];
    cluster = (uint16_t)f->payload[3] | ((uint16_t)f->payload[4] << 8);
    dtype   = f->payload[5];
    vlen    = f->payload[6];
    val     = &f->payload[7];
    if ((uint16_t)(7 + vlen) > f->plen) return;

    /* 协调器离线约定：ep=0 + cluster=0 + vlen=1 + val=0 → 整节点离线 */
    if (ep == 0 && cluster == 0 && vlen == 1 && val[0] == 0) {
        ns = slot_touch(nodeid);
        if (ns) { ns->online = 0; }
        automation_on_offline(nodeid);
        return;
    }

    ns = slot_touch(nodeid);
    if (!ns) return;
    ns->online = 1;
    ns->last_update_ms = ms_now();

    switch (cluster) {
    case ZCL_CLU_TEMP:
        if (dtype == DTYPE_I16 && vlen == 2) {
            ns->temp_x100 = (int16_t)((uint16_t)val[0] | ((uint16_t)val[1] << 8));
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_HUM:
        if (dtype == DTYPE_U16 && vlen == 2) {
            ns->hum_x100 = (uint16_t)val[0] | ((uint16_t)val[1] << 8);
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_LUX:
        if (dtype == DTYPE_U16 && vlen == 2) {
            ns->lux = (uint16_t)val[0] | ((uint16_t)val[1] << 8);
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_ONOFF:
        if (dtype == DTYPE_BOOL && vlen == 1) {
            if (ep == 1) ns->led_state = val[0] ? 1 : 0;
            else if (ep == 2) ns->buzzer_state = val[0] ? 1 : 0;
            emit_status_json(nodeid, ns);
        }
        break;
    default:
        break;
    }

    automation_on_sample(nodeid, ns);
}

void router_on_zb_frame(const frame_t *f)
{
    switch (f->cmd) {
    case CMD_ZB_REPORT:    on_zb_report(f); break;
    case CMD_ZB_NET_STATUS: {
        /* u8 state;u8 ch;u16 panid;u8 joined_n → 只 log */
        LOGI("zb", "NET state=%u ch=%u pan=%04X n=%u",
             f->payload[0], f->payload[1],
             (unsigned)((uint16_t)f->payload[2] | ((uint16_t)f->payload[3] << 8)),
             f->payload[4]);
        break;
    }
    case CMD_ZB_ERR:
        LOGW("zb", "ERR code=0x%02X ctx=0x%02X", f->payload[0], f->payload[1]);
        break;
    default: break;
    }
}

/* ---------- 下行：JSON → ZB ---------- */
static void send_ack(int32_t seq, uint8_t ok, const char *err)
{
    char buf[96];
    int n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "ack");
    n = json_add_int(buf, sizeof(buf), (uint16_t)n, "seq", seq);
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "ok", ok ? "true" : "false");
    if (err) n = json_add_str(buf, sizeof(buf), (uint16_t)n, "err", err);
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) (void)wifi_link_send_line(buf, (uint16_t)n);
}

int router_cmd_onoff(uint16_t nodeid, uint8_t ep, uint8_t onoff)
{
    uint8_t pl[8];
    pl[0] = (uint8_t)(nodeid & 0xFF);
    pl[1] = (uint8_t)(nodeid >> 8);
    pl[2] = ep;
    pl[3] = (uint8_t)(ZCL_CLU_ONOFF & 0xFF);
    pl[4] = (uint8_t)(ZCL_CLU_ONOFF >> 8);
    pl[5] = DTYPE_BOOL;
    pl[6] = 1;
    pl[7] = onoff ? 1 : 0;
    return zb_link_send(CMD_ZB_CMD, pl, sizeof(pl));
}

static void on_cmd_json(const char *line, uint16_t n)
{
    char t[16], target[16], op[8];
    int32_t seq = 0;
    uint32_t node = 0;
    uint8_t ep;
    uint8_t onoff;

    if (json_get_string(line, n, "target", target, sizeof(target)) != 0) { send_ack(0, 0, "bad"); return; }
    if (json_get_string(line, n, "op", op, sizeof(op)) != 0) { send_ack(0, 0, "bad"); return; }
    (void)json_get_int(line, n, "seq", &seq);
    if (json_get_uint(line, n, "node", &node) != 0) { send_ack(seq, 0, "node"); return; }

    if (strcmp(target, "led") == 0) ep = 1;
    else if (strcmp(target, "buzzer") == 0) ep = 2;
    else { send_ack(seq, 0, "target"); return; }

    if (strcmp(op, "on") == 0) onoff = 1;
    else if (strcmp(op, "off") == 0) onoff = 0;
    else { send_ack(seq, 0, "op"); return; }

    if (router_cmd_onoff((uint16_t)node, ep, onoff) != 0) { send_ack(seq, 0, "tx"); return; }
    send_ack(seq, 1, 0);
    (void)t;
}

static void on_set_threshold(const char *line, uint16_t n)
{
    thresholds_t th;
    int32_t seq = 0; int32_t v32; uint32_t vu32; float vf;
    (void)config_load(&th);
    (void)json_get_int(line, n, "seq", &seq);
    if (json_get_uint(line, n, "lux_low", &vu32) == 0) th.lux_low = (uint16_t)vu32;
    if (json_get_float(line, n, "temp_high", &vf) == 0) th.temp_high_x100 = (int16_t)(vf * 100);
    if (json_get_float(line, n, "temp_low",  &vf) == 0) th.temp_low_x100  = (int16_t)(vf * 100);
    if (json_get_float(line, n, "hum_high",  &vf) == 0) th.hum_high_x100  = (uint16_t)(vf * 100);
    if (json_get_float(line, n, "hum_low",   &vf) == 0) th.hum_low_x100   = (uint16_t)(vf * 100);
    if (config_save(&th) != 0) { send_ack(seq, 0, "nv"); return; }
    automation_reload(&th);
    send_ack(seq, 1, 0);
    (void)v32;
}

static void on_allow_join(const char *line, uint16_t n)
{
    int32_t seq = 0; uint32_t sec = 60;
    uint8_t pl;
    (void)json_get_int(line, n, "seq", &seq);
    (void)json_get_uint(line, n, "sec", &sec);
    if (sec > 255) sec = 255;
    pl = (uint8_t)sec;
    if (zb_link_send(CMD_ZB_ALLOW_JOIN, &pl, 1) != 0) send_ack(seq, 0, "tx");
    else send_ack(seq, 1, 0);
}

void router_on_json_line(const char *line, uint16_t n)
{
    char t[16];
    if (json_get_string(line, n, "t", t, sizeof(t)) != 0) return;
    if      (strcmp(t, "cmd") == 0)            on_cmd_json(line, n);
    else if (strcmp(t, "set_threshold") == 0)  on_set_threshold(line, n);
    else if (strcmp(t, "allow_join") == 0)     on_allow_join(line, n);
    else if (strcmp(t, "list_nodes") == 0)     { uint8_t zero = 0; (void)zb_link_send(CMD_ZB_LIST_NODES, &zero, 0); (void)zero; }
    else if (strcmp(t, "ping") == 0) {
        char buf[48];
        int k = json_begin(buf, sizeof(buf));
        k = json_add_str (buf, sizeof(buf), (uint16_t)k, "t", "pong");
        k = json_add_uint(buf, sizeof(buf), (uint16_t)k, "ts", ms_now());
        k = json_end(buf, sizeof(buf), (uint16_t)k);
        if (k > 0) (void)wifi_link_send_line(buf, (uint16_t)k);
    }
}
