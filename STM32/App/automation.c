/*
 * Automatic linkage state machine for light and temp/humidity alarms.
 */

#include "automation.h"
#include "config.h"
#include "json.h"
#include "wifi_link.h"
#include "drv_tim.h"
#include <string.h>
#include <stdio.h>

static thresholds_t s_th;
static char s_alarm_text[24] = "";

typedef struct {
    uint8_t  pending_state;
    uint8_t  cur_state;
    uint32_t pending_since_ms;
} rule_t;

static rule_t s_rule_light = { 0, 0, 0 };
static rule_t s_rule_temp  = { 0, 0, 0 };
static uint32_t s_light_override_until_ms = 0;
static uint32_t s_alarm_override_until_ms = 0;

static void emit_alarm(const char *type, uint8_t on, int32_t val, int32_t threshold)
{
    char buf[128];
    int n;

    n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "alarm");
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "type", type);
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "level", on ? "on" : "off");
    n = json_add_int(buf, sizeof(buf), (uint16_t)n, "val", val);
    n = json_add_int(buf, sizeof(buf), (uint16_t)n, "threshold", threshold);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) {
        (void)wifi_link_send_line(buf, (uint16_t)n);
    }

    if (on) {
        snprintf(s_alarm_text, sizeof(s_alarm_text), "%s-ON", type);
    } else {
        s_alarm_text[0] = '\0';
    }
}

static void apply_pending(rule_t *r, uint8_t want, uint32_t now_ms, uint32_t debounce_ms)
{
    if (r->pending_state != want) {
        r->pending_state = want;
        r->pending_since_ms = now_ms;
    } else if ((now_ms - r->pending_since_ms) >= debounce_ms) {
        if (r->cur_state != want) {
            r->cur_state = 0xFFu;
        }
    }
}

static void clear_rule_if_stale(rule_t *rule, const char *alarm_type, uint32_t threshold)
{
    if (rule->cur_state) {
        rule->cur_state = 0;
        rule->pending_state = 0;
        rule->pending_since_ms = 0;
        emit_alarm(alarm_type, 0, 0, (int32_t)threshold);
    }
}

static const char *node1_alarm_type(void)
{
    return (strncmp(s_alarm_text, "hum", 3) == 0) ? "hum" : "temp";
}

static uint32_t node1_alarm_threshold(void)
{
    return (strncmp(s_alarm_text, "hum", 3) == 0) ? s_th.hum_high_x100 : (uint32_t)s_th.temp_high_x100;
}

void automation_init(const thresholds_t *th)
{
    s_th = *th;
    memset(&s_rule_light, 0, sizeof(s_rule_light));
    memset(&s_rule_temp, 0, sizeof(s_rule_temp));
    s_alarm_text[0] = '\0';
    s_light_override_until_ms = 0;
    s_alarm_override_until_ms = 0;
}

void automation_reload(const thresholds_t *th)
{
    s_th = *th;
}

void automation_manual_override(uint16_t nodeid, uint8_t ep, uint32_t now_ms)
{
    if (nodeid == router_node2_id() && ep == 1u) {
        s_light_override_until_ms = now_ms + AUTO_OVERRIDE_MS;
    } else if (nodeid == router_node1_id() && ep == 2u) {
        s_alarm_override_until_ms = now_ms + AUTO_OVERRIDE_MS;
    }
}

const char *automation_alarm_text(void)
{
    return s_alarm_text;
}

void automation_on_sample(uint16_t nodeid, const node_state_t *ns)
{
    uint32_t now = ms_now();
    uint16_t n2 = router_node2_id();
    uint16_t n1 = router_node1_id();

    if (nodeid == n2 && ns->online) {
        uint8_t want;

        if (now < s_light_override_until_ms) {
            return;
        }

        if (ns->lux < s_th.lux_low) {
            want = 1u;
        } else if (ns->lux > (uint16_t)(s_th.lux_low + s_th.hysteresis_lux)) {
            want = 0u;
        } else {
            want = s_rule_light.cur_state;
        }
        apply_pending(&s_rule_light, want, now, s_th.debounce_ms);
        if (s_rule_light.cur_state == 0xFFu) {
            s_rule_light.cur_state = s_rule_light.pending_state;
            (void)router_cmd_onoff(n2, 1u, s_rule_light.cur_state);
            emit_alarm("light", s_rule_light.cur_state, ns->lux, s_th.lux_low);
        }
    }

    if (nodeid == n1 && ns->online) {
        uint8_t want;
        int16_t t = ns->temp_x100;
        uint16_t h = ns->hum_x100;
        uint8_t temp_high = (t > s_th.temp_high_x100);
        uint8_t temp_low = (t < s_th.temp_low_x100);
        uint8_t hum_high = (h > s_th.hum_high_x100);
        uint8_t hum_low = (h < s_th.hum_low_x100);

        if (now < s_alarm_override_until_ms) {
            return;
        }

        if (temp_high || temp_low || hum_high || hum_low) {
            want = 1u;
        } else if (s_rule_temp.cur_state &&
                   (t >= (int16_t)(s_th.temp_low_x100 + s_th.hysteresis_temp_x100)) &&
                   (t <= (int16_t)(s_th.temp_high_x100 - s_th.hysteresis_temp_x100)) &&
                   (h >= s_th.hum_low_x100) &&
                   (h <= s_th.hum_high_x100)) {
            want = 0u;
        } else {
            want = s_rule_temp.cur_state;
        }

        apply_pending(&s_rule_temp, want, now, s_th.debounce_ms);
        if (s_rule_temp.cur_state == 0xFFu) {
            s_rule_temp.cur_state = s_rule_temp.pending_state;
            (void)router_cmd_onoff(n1, 2u, s_rule_temp.cur_state);
            if (temp_high) {
                emit_alarm("temp", s_rule_temp.cur_state, t, s_th.temp_high_x100);
            } else if (temp_low) {
                emit_alarm("temp", s_rule_temp.cur_state, t, s_th.temp_low_x100);
            } else if (hum_high) {
                emit_alarm("hum", s_rule_temp.cur_state, h, s_th.hum_high_x100);
            } else {
                emit_alarm("hum", s_rule_temp.cur_state, h, s_th.hum_low_x100);
            }
        }
    }
}

void automation_on_offline(uint16_t nodeid)
{
    if (nodeid == router_node2_id()) {
        clear_rule_if_stale(&s_rule_light, "light", s_th.lux_low);
    }
    if (nodeid == router_node1_id()) {
        clear_rule_if_stale(&s_rule_temp, node1_alarm_type(), node1_alarm_threshold());
    }
}

void automation_tick(uint32_t now_ms)
{
    const node_state_t *n1 = router_node(router_node1_id());
    const node_state_t *n2 = router_node(router_node2_id());

    if (n1 != 0 && n1->online && (now_ms - n1->last_update_ms) > NODE1_STALE_MS) {
        clear_rule_if_stale(&s_rule_temp, node1_alarm_type(), node1_alarm_threshold());
    }
    if (n2 != 0 && n2->online && (now_ms - n2->last_update_ms) > NODE2_STALE_MS) {
        clear_rule_if_stale(&s_rule_light, "light", s_th.lux_low);
    }
}
