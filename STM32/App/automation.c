/*
 * 自动联动状态机（§5.4）：
 *   光照：lux < lux_low 持续 debounce_ms → Node2 LED on；
 *         lux > lux_low + hysteresis_lux 持续 debounce_ms → off；
 *   温湿：任一越限 debounce_ms → Node1 Buzzer on；恢复 debounce_ms → off。
 * 报警变化即上行 alarm JSON。
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
    uint8_t  pending_state;      /* 正在去抖的目标：1=on, 0=off */
    uint8_t  cur_state;
    uint32_t pending_since_ms;
} rule_t;

static rule_t s_rule_light = { 0, 0, 0 };
static rule_t s_rule_temp  = { 0, 0, 0 };
static uint32_t s_manual_override_until_ms = 0;

void automation_init(const thresholds_t *th)
{
    s_th = *th;
    memset(&s_rule_light, 0, sizeof(s_rule_light));
    memset(&s_rule_temp, 0, sizeof(s_rule_temp));
    s_alarm_text[0] = '\0';
}

void automation_reload(const thresholds_t *th) { s_th = *th; }

const char *automation_alarm_text(void) { return s_alarm_text; }

static void emit_alarm(const char *type, uint8_t on, int32_t val, int32_t threshold)
{
    char buf[128];
    int n = json_begin(buf, sizeof(buf));
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "t", "alarm");
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "type", type);
    n = json_add_str (buf, sizeof(buf), (uint16_t)n, "level", on ? "on" : "off");
    n = json_add_int (buf, sizeof(buf), (uint16_t)n, "val", val);
    n = json_add_int (buf, sizeof(buf), (uint16_t)n, "threshold", threshold);
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "ts", ms_now());
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) (void)wifi_link_send_line(buf, (uint16_t)n);

    if (on) snprintf(s_alarm_text, sizeof(s_alarm_text), "%s-ON", type);
    else    s_alarm_text[0] = '\0';
}

static void apply_pending(rule_t *r, uint8_t want, uint32_t now_ms, uint32_t debounce_ms)
{
    if (r->pending_state != want) {
        r->pending_state = want;
        r->pending_since_ms = now_ms;
    } else if ((now_ms - r->pending_since_ms) >= debounce_ms) {
        if (r->cur_state != want) r->cur_state = 0xFF;   /* 触发 */
    }
}

void automation_on_sample(uint16_t nodeid, const node_state_t *ns)
{
    uint32_t now = ms_now();
    uint16_t n2 = router_node2_id();
    uint16_t n1 = router_node1_id();

    if (now < s_manual_override_until_ms) return;

    if (nodeid == n2 && ns->online) {
        uint8_t want;
        if (ns->lux < s_th.lux_low) want = 1;
        else if (ns->lux > (uint16_t)(s_th.lux_low + s_th.hysteresis_lux)) want = 0;
        else want = s_rule_light.cur_state;
        apply_pending(&s_rule_light, want, now, s_th.debounce_ms);
        if (s_rule_light.cur_state == 0xFF) {
            s_rule_light.cur_state = s_rule_light.pending_state;
            (void)router_cmd_onoff(n2, 1, s_rule_light.cur_state);
            emit_alarm("light", s_rule_light.cur_state, ns->lux, s_th.lux_low);
        }
    }

    if (nodeid == n1 && ns->online) {
        uint8_t want;
        int16_t t = ns->temp_x100;
        uint16_t h = ns->hum_x100;
        if (t > s_th.temp_high_x100 || t < s_th.temp_low_x100 ||
            h > s_th.hum_high_x100  || h < s_th.hum_low_x100) {
            want = 1;
        } else {
            want = 0;
        }
        apply_pending(&s_rule_temp, want, now, s_th.debounce_ms);
        if (s_rule_temp.cur_state == 0xFF) {
            s_rule_temp.cur_state = s_rule_temp.pending_state;
            (void)router_cmd_onoff(n1, 2, s_rule_temp.cur_state);
            emit_alarm(t > s_th.temp_high_x100 || t < s_th.temp_low_x100 ? "temp" : "hum",
                       s_rule_temp.cur_state, (int32_t)t, (int32_t)s_th.temp_high_x100);
        }
    }
}

void automation_on_offline(uint16_t nodeid)
{
    uint16_t n2 = router_node2_id();
    uint16_t n1 = router_node1_id();
    if (nodeid == n2 && s_rule_light.cur_state) {
        s_rule_light.cur_state = 0;
        emit_alarm("light", 0, 0, s_th.lux_low);
    }
    if (nodeid == n1 && s_rule_temp.cur_state) {
        s_rule_temp.cur_state = 0;
        emit_alarm("temp", 0, 0, s_th.temp_high_x100);
    }
}

void automation_tick(uint32_t now_ms) { (void)now_ms; }
