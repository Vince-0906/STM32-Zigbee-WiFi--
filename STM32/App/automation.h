#ifndef __APP_AUTOMATION_H__
#define __APP_AUTOMATION_H__

#include "router.h"
#include "thresholds.h"

#ifdef __cplusplus
extern "C" {
#endif

void automation_init(const thresholds_t *th);
void automation_reload(const thresholds_t *th);
void automation_on_sample(uint16_t nodeid, const node_state_t *ns);
void automation_on_offline(uint16_t nodeid);
void automation_tick(uint32_t now_ms);

/* 最近一次报警文字（给 OLED），无报警时返回 ""。 */
const char *automation_alarm_text(void);

#ifdef __cplusplus
}
#endif

#endif
