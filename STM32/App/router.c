/*
 * Router: translate Zigbee UART frames to JSON and maintain gateway-side
 * node state. Node1/Node2 are bound by business role instead of join order.
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
static uint8_t      s_last_joined = 0xFFu;

static uint16_t s_n1_id = 0;
static uint16_t s_n2_id = 0;

static node_state_t *slot_find(uint16_t nodeid)
{
    uint8_t i;

    for (i = 0; i < s_idcnt; ++i) {
        if (s_idmap[i] == nodeid) {
            return &s_nodes[i];
        }
    }
    return 0;
}

static node_state_t *slot_touch(uint16_t nodeid)
{
    node_state_t *ns;

    ns = slot_find(nodeid);
    if (ns != 0) {
        return ns;
    }
    if (s_idcnt >= ROUTER_NODE_MAX) {
        return 0;
    }

    s_idmap[s_idcnt] = nodeid;
    memset(&s_nodes[s_idcnt], 0, sizeof(node_state_t));
    return &s_nodes[s_idcnt++];
}

static void bind_role(uint16_t nodeid, uint8_t role)
{
    if (role == ZB_ROLE_TEMP_HUM) {
        s_n1_id = nodeid;
    } else if (role == ZB_ROLE_LUX) {
        s_n2_id = nodeid;
    }
}

static uint8_t role_from_cluster(uint16_t cluster)
{
    if (cluster == ZCL_CLU_TEMP || cluster == ZCL_CLU_HUM) {
        return ZB_ROLE_TEMP_HUM;
    }
    if (cluster == ZCL_CLU_LUX) {
        return ZB_ROLE_LUX;
    }
    return ZB_ROLE_UNKNOWN;
}

static const char *role_name(uint8_t role)
{
    if (role == ZB_ROLE_TEMP_HUM) {
        return "temp_hum";
    }
    if (role == ZB_ROLE_LUX) {
        return "lux";
    }
    return "unknown";
}

static const char *dev_name(uint8_t dev_type)
{
    if (dev_type == ZB_DEV_ROUTER) {
        return "router";
    }
    if (dev_type == ZB_DEV_ENDDEV) {
        return "enddev";
    }
    return "unknown";
}

static void request_node_snapshot(void)
{
    LOGI("zb", "tx LIST_NODES src=pc/router");
    (void)zb_link_send(CMD_ZB_LIST_NODES, 0, 0);
}

static void update_identity(node_state_t *ns, uint16_t nodeid, uint8_t role,
                            uint8_t dev_type, int8_t rssi, uint8_t last_seen_s,
                            uint8_t have_last_seen, int8_t online_hint)
{
    if (role != ZB_ROLE_UNKNOWN) {
        ns->role = role;
        bind_role(nodeid, role);
    }
    if (dev_type != ZB_DEV_UNKNOWN) {
        ns->dev_type = dev_type;
    }
    ns->rssi = rssi;
    if (have_last_seen) {
        ns->last_seen_s = last_seen_s;
    }
    if (online_hint >= 0) {
        ns->online = (online_hint != 0) ? 1u : 0u;
    }
}

uint16_t router_node1_id(void) { return s_n1_id; }
uint16_t router_node2_id(void) { return s_n2_id; }
uint8_t router_node_count(void) { return s_idcnt; }

const node_state_t *router_node(uint16_t nodeid)
{
    return slot_find(nodeid);
}

void router_init(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_idmap, 0, sizeof(s_idmap));
    s_idcnt = 0;
    s_n1_id = 0;
    s_n2_id = 0;
    s_last_joined = 0xFFu;
}

static void emit_report_json(uint16_t nodeid, uint16_t cluster, const node_state_t *ns)
{
    char buf[128];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "report");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "node", nodeid);
    if (cluster == ZCL_CLU_TEMP || cluster == ZCL_CLU_HUM) {
        n = json_add_str(buf, sizeof(buf), (uint16_t)n, "kind", "temp_hum");
        n = json_add_fp2(buf, sizeof(buf), (uint16_t)n, "temp", ns->temp_x100);
        n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "hum", (uint32_t)(ns->hum_x100 / 100u));
    } else if (cluster == ZCL_CLU_LUX) {
        n = json_add_str(buf, sizeof(buf), (uint16_t)n, "kind", "lux");
        n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "lux", (uint32_t)ns->lux);
    } else {
        return;
    }
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }
}

static void emit_status_json(uint16_t nodeid, const node_state_t *ns)
{
    char buf[96];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "status");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "node", nodeid);
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "led", ns->led_state ? "on" : "off");
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "buzzer", ns->buzzer_state ? "on" : "off");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }
}

static void emit_node_info_json(uint16_t nodeid, const node_state_t *ns)
{
    char buf[160];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "node_info");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "node", nodeid);
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "role", role_name(ns->role));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "dev", dev_name(ns->dev_type));
    n = json_add_int(buf, sizeof(buf), (uint16_t)n, "rssi", ns->rssi);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "last_seen_s", ns->last_seen_s);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "online", ns->online);
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }
}

static void emit_net_json(uint8_t state, uint8_t channel, uint16_t panid, uint8_t joined)
{
    char buf[96];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "net");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "state", state);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "channel", channel);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "panid", panid);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "joined", joined);
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }
}

static void on_zb_report(const frame_t *f)
{
    uint16_t nodeid;
    uint8_t ep;
    uint16_t cluster;
    uint8_t dtype;
    uint8_t vlen;
    const uint8_t *val;
    node_state_t *ns;
    uint8_t role;
    uint8_t role_changed;

    if (f->plen < 7u) {
        return;
    }

    nodeid  = (uint16_t)f->payload[0] | ((uint16_t)f->payload[1] << 8);
    ep      = f->payload[2];
    cluster = (uint16_t)f->payload[3] | ((uint16_t)f->payload[4] << 8);
    dtype   = f->payload[5];
    vlen    = f->payload[6];
    val     = &f->payload[7];
    if ((uint16_t)(7u + vlen) > f->plen) {
        return;
    }

    if (vlen >= 2u) {
        LOGI("zb", "REPORT id=%04X ep=%u clu=%04X dt=%02X v=0x%02X%02X",
             (unsigned)nodeid, ep, (unsigned)cluster, dtype, val[1], val[0]);
    } else {
        LOGI("zb", "REPORT id=%04X ep=%u clu=%04X dt=%02X vlen=%u",
             (unsigned)nodeid, ep, (unsigned)cluster, dtype, vlen);
    }

    if (ep == 0u && cluster == 0u && vlen == 1u && val[0] == 0u) {
        ns = slot_touch(nodeid);
        if (ns != 0) {
            ns->online = 0;
            ns->last_seen_s = ZB_NODE_OFFLINE_SEC;
            emit_node_info_json(nodeid, ns);
        }
        automation_on_offline(nodeid);
        return;
    }

    ns = slot_touch(nodeid);
    if (ns == 0) {
        return;
    }

    ns->online = 1;
    ns->last_update_ms = ms_now();
    ns->last_seen_s = 0;
    update_identity(ns, nodeid, ZB_ROLE_UNKNOWN, ZB_DEV_ENDDEV, ns->rssi, 0, 0, 1);

    role = role_from_cluster(cluster);
    role_changed = 0;
    if (role != ZB_ROLE_UNKNOWN && ns->role != role) {
        role_changed = 1u;
    }
    if (role != ZB_ROLE_UNKNOWN) {
        update_identity(ns, nodeid, role, ZB_DEV_ENDDEV, ns->rssi, 0, 0, 1);
    }

    switch (cluster) {
    case ZCL_CLU_TEMP:
        if (dtype == DTYPE_I16 && vlen == 2u) {
            ns->temp_x100 = (int16_t)((uint16_t)val[0] | ((uint16_t)val[1] << 8));
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_HUM:
        if (dtype == DTYPE_U16 && vlen == 2u) {
            ns->hum_x100 = (uint16_t)val[0] | ((uint16_t)val[1] << 8);
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_LUX:
        if (dtype == DTYPE_U16 && vlen == 2u) {
            ns->lux = (uint16_t)val[0] | ((uint16_t)val[1] << 8);
            emit_report_json(nodeid, cluster, ns);
        }
        break;
    case ZCL_CLU_ONOFF:
        if (dtype == DTYPE_BOOL && vlen == 1u) {
            if (ep == 1u) {
                ns->led_state = val[0] ? 1u : 0u;
            } else if (ep == 2u) {
                ns->buzzer_state = val[0] ? 1u : 0u;
            }
            emit_status_json(nodeid, ns);
        }
        break;
    default:
        break;
    }

    if (role_changed) {
        emit_node_info_json(nodeid, ns);
    }
    automation_on_sample(nodeid, ns);
}

static void on_zb_node_info(const frame_t *f)
{
    uint16_t nodeid;
    uint8_t dev_type;
    int8_t rssi;
    uint8_t last_seen_s;
    uint8_t role;
    node_state_t *ns;

    if (f->plen < 7u) {
        return;
    }

    nodeid      = (uint16_t)f->payload[0] | ((uint16_t)f->payload[1] << 8);
    dev_type    = f->payload[2];
    rssi        = (int8_t)f->payload[3];
    last_seen_s = f->payload[4];
    role        = f->payload[5];

    ns = slot_touch(nodeid);
    if (ns == 0) {
        return;
    }

    update_identity(ns, nodeid, role, dev_type, rssi, last_seen_s, 1, (int8_t)f->payload[6]);
    if (f->payload[6] == 0u) {
        automation_on_offline(nodeid);
    }
    LOGI("zb", "NODE id=%04X role=%s dev=%s rssi=%d last=%u on=%u",
         (unsigned)nodeid, role_name(ns->role), dev_name(ns->dev_type),
         ns->rssi, ns->last_seen_s, ns->online);
    LOGI("zb", "NODEINFO id=%04X role=%u dev=%u on=%u rssi=%d",
         (unsigned)nodeid, role, dev_type, f->payload[6], rssi);
    emit_node_info_json(nodeid, ns);
}

void router_on_zb_frame(const frame_t *f)
{
    uint16_t panid;
    uint8_t joined;

    switch (f->cmd) {
    case CMD_ZB_REPORT:
        on_zb_report(f);
        break;
    case CMD_ZB_NODE_INFO:
        on_zb_node_info(f);
        break;
    case CMD_ZB_NET_STATUS:
        if (f->plen >= 5u) {
            joined = f->payload[4];
            panid = (uint16_t)f->payload[2] | ((uint16_t)f->payload[3] << 8);
            emit_net_json(f->payload[0], f->payload[1], panid, joined);
            LOGI("zb", "NET state=%u ch=%u pan=%04X n=%u",
                 f->payload[0], f->payload[1], (unsigned)panid, joined);
            if (joined != s_last_joined) {
                s_last_joined = joined;
                request_node_snapshot();
            }
        }
        break;
    case CMD_ZB_ERR:
        if (f->plen >= 2u) {
            LOGW("zb", "ERR code=0x%02X ctx=0x%02X", f->payload[0], f->payload[1]);
        }
        break;
    default:
        break;
    }
}

static void send_ack(int32_t seq, uint8_t ok, const char *err)
{
    char buf[96];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "ack");
    n = json_add_int(buf, sizeof(buf), (uint16_t)n, "seq", seq);
    n = json_add_bool(buf, sizeof(buf), (uint16_t)n, "ok", ok);
    if (err) {
        n = json_add_str(buf, sizeof(buf), (uint16_t)n, "err", err);
    }
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }
}

int router_cmd_onoff(uint16_t nodeid, uint8_t ep, uint8_t onoff)
{
    uint8_t pl[8];

    pl[0] = (uint8_t)(nodeid & 0xFFu);
    pl[1] = (uint8_t)(nodeid >> 8);
    pl[2] = ep;
    pl[3] = (uint8_t)(ZCL_CLU_ONOFF & 0xFFu);
    pl[4] = (uint8_t)(ZCL_CLU_ONOFF >> 8);
    pl[5] = DTYPE_BOOL;
    pl[6] = 1u;
    pl[7] = onoff ? 1u : 0u;
    return zb_link_send(CMD_ZB_CMD, pl, sizeof(pl));
}

static void on_cmd_json(const char *line, uint16_t n)
{
    char target[16];
    char op[8];
    int32_t seq;
    uint32_t node;
    uint8_t ep;
    uint8_t onoff;
    const node_state_t *ns;

    seq = 0;
    node = 0;

    if (json_get_string(line, n, "target", target, sizeof(target)) != 0) {
        send_ack(0, 0, "bad");
        return;
    }
    if (json_get_string(line, n, "op", op, sizeof(op)) != 0) {
        send_ack(0, 0, "bad");
        return;
    }
    (void)json_get_int(line, n, "seq", &seq);
    if (json_get_uint(line, n, "node", &node) != 0) {
        send_ack(seq, 0, "node");
        return;
    }

    if (strcmp(target, "led") == 0) {
        ep = 1u;
    } else if (strcmp(target, "buzzer") == 0) {
        ep = 2u;
    } else {
        send_ack(seq, 0, "target");
        return;
    }

    if (strcmp(op, "on") == 0) {
        onoff = 1u;
    } else if (strcmp(op, "off") == 0) {
        onoff = 0u;
    } else if (strcmp(op, "toggle") == 0) {
        /* 使用网关缓存的执行器状态翻转；缓存未知按关处理（上次确认为关或未知）。 */
        ns = router_node((uint16_t)node);
        if (ns == 0) {
            send_ack(seq, 0, "node");
            return;
        }
        if (ep == 1u) {
            onoff = (uint8_t)(ns->led_state ? 0u : 1u);
        } else {
            onoff = (uint8_t)(ns->buzzer_state ? 0u : 1u);
        }
    } else {
        send_ack(seq, 0, "op");
        return;
    }

    if (router_cmd_onoff((uint16_t)node, ep, onoff) != 0) {
        send_ack(seq, 0, "tx");
        return;
    }
    automation_manual_override((uint16_t)node, ep, ms_now());
    send_ack(seq, 1, 0);
}

static void on_set_threshold(const char *line, uint16_t n)
{
    thresholds_t th;
    int32_t seq;
    uint32_t vu32;
    float vf;

    seq = 0;
    vu32 = 0;
    vf = 0.0f;

    (void)config_load(&th);
    (void)json_get_int(line, n, "seq", &seq);
    if (json_get_uint(line, n, "lux_low", &vu32) == 0) {
        th.lux_low = (uint16_t)vu32;
    }
    if (json_get_float(line, n, "temp_high", &vf) == 0) {
        th.temp_high_x100 = (int16_t)(vf * 100.0f);
    }
    if (json_get_float(line, n, "temp_low", &vf) == 0) {
        th.temp_low_x100 = (int16_t)(vf * 100.0f);
    }
    if (json_get_float(line, n, "hum_high", &vf) == 0) {
        th.hum_high_x100 = (uint16_t)(vf * 100.0f);
    }
    if (json_get_float(line, n, "hum_low", &vf) == 0) {
        th.hum_low_x100 = (uint16_t)(vf * 100.0f);
    }
    if (config_save(&th) != 0) {
        send_ack(seq, 0, "nv");
        return;
    }
    automation_reload(&th);
    send_ack(seq, 1, 0);
}

static void on_allow_join(const char *line, uint16_t n)
{
    int32_t seq;
    uint32_t sec;
    uint8_t pl;

    seq = 0;
    sec = 60u;

    (void)json_get_int(line, n, "seq", &seq);
    (void)json_get_uint(line, n, "sec", &sec);
    if (sec > 255u) {
        sec = 255u;
    }
    pl = (uint8_t)sec;
    LOGI("zb", "tx ALLOW_JOIN sec=%u src=pc/router", pl);
    if (zb_link_send(CMD_ZB_ALLOW_JOIN, &pl, 1) != 0) {
        send_ack(seq, 0, "tx");
    } else {
        request_node_snapshot();
        send_ack(seq, 1, 0);
    }
}

void router_on_json_line(const char *line, uint16_t n)
{
    char t[16];

    if (json_get_string(line, n, "t", t, sizeof(t)) != 0) {
        return;
    }

    if (strcmp(t, "cmd") == 0) {
        on_cmd_json(line, n);
    } else if (strcmp(t, "set_threshold") == 0) {
        on_set_threshold(line, n);
    } else if (strcmp(t, "allow_join") == 0) {
        on_allow_join(line, n);
    } else if (strcmp(t, "list_nodes") == 0) {
        request_node_snapshot();
    } else if (strcmp(t, "ping") == 0) {
        char buf[48];
        int k;

        k = json_begin(buf, sizeof(buf));
        k = json_add_str(buf, sizeof(buf), (uint16_t)k, "t", "pong");
        k = json_add_uint(buf, sizeof(buf), (uint16_t)k, "ts", ms_now());
        k = json_end(buf, sizeof(buf), (uint16_t)k);
        if (k > 0) {
            (void)wifi_link_send_line(buf, (uint16_t)k);
        }
    }
}
