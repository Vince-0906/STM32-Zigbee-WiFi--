#ifndef __ZB_NODE_H__
#define __ZB_NODE_H__

#include "ZComDef.h"
#include "board_e18ms1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AF 端点（规范书 §4.4） */
#define ZBNODE_EP_SENSOR    0x01u       /* 传感器 + LED OnOff */
#define ZBNODE_EP_BUZZER    0x02u       /* 蜂鸣器 OnOff */

#define ZBNODE_PROFILE_ID   0x0104u     /* HA Profile */
#define ZBNODE_DEVICE_ID    0x0000u
#define ZBNODE_DEVICE_VER   1

/* OSAL 事件位 */
#define ZN_EVT_REPORT       0x0001u

extern uint8 ZbNode_TaskID;

extern void   ZbNode_Init(uint8 task_id);
extern UINT16 ZbNode_ProcessEvent(uint8 task_id, uint16 events);

#ifdef __cplusplus
}
#endif

#endif /* __ZB_NODE_H__ */
