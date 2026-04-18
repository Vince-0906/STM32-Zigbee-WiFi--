#ifndef __NODE_OLED_VIEW_H__
#define __NODE_OLED_VIEW_H__

#include "ZComDef.h"

/* 节点 OLED 4 行视图模型：
 *  Row0: "N1 TempHum" / "N2 Lux"   + 网络状态
 *  Row1: "ID:nnnn  CH:xx"
 *  Row2: 传感数据（T/H 或 L）
 *  Row3: "LED:ON  BUZ:OFF"
 */

typedef struct {
    uint8  role;                /* 1 = TempHum, 2 = Lux */
    uint8  online;              /* 是否已加入网络 */
    uint16 short_addr;          /* nwk 短地址 */
    uint8  channel;
    int16  temp_x100;
    uint16 hum_x100;
    uint16 lux;
    uint8  led_on;
    uint8  buzzer_on;
    uint8  has_sample;          /* 是否拿到过一次有效采样 */
} node_oled_model_t;

void node_oled_view_init(uint8 role);
void node_oled_view_set(const node_oled_model_t *m);   /* 仅更新模型缓存 */
void node_oled_view_refresh(void);                     /* 真正写屏 */

#endif
