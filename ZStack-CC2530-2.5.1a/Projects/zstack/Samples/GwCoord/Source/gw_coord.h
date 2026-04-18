#ifndef __GW_COORD_H__
#define __GW_COORD_H__

/*
 * GwCoord 主任务头文件（规范书 §4.5 + CC2530.md §3）。
 */

#include "ZComDef.h"
#include "gw_types.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- AF 端点定义 ---------- */
#define GWCOORD_ENDPOINT        0x01u
#define GWCOORD_PROFILE_ID      0x0104u        /* HA Profile */
#define GWCOORD_DEVICE_ID       0x0000u        /* OnOffLight/Generic */
#define GWCOORD_DEVICE_VERSION  1
#define GWCOORD_FLAGS           0

/* 监听的 cluster（入站）：温/湿/照度/OnOff */
#define GWCOORD_CLUSTERS_MAX    4u

/* ---------- 任务 ID 暴露 ---------- */
extern uint8 GwCoord_TaskID;

/* ---------- OSAL 约定接口 ---------- */
extern void   GwCoord_Init(uint8 task_id);
extern UINT16 GwCoord_ProcessEvent(uint8 task_id, uint16 events);

/* 由 uart_link 在收到完整帧时调用；执行于 OSAL 任务上下文。 */
extern void   GwCoord_OnFrame(const frame_t *f);

/* 组帧并通过 UART 发送；0=OK, <0=err。 */
extern int8   GwCoord_SendFrame(uint8 cmd, const uint8 *payload, uint8 plen);

/* 上报 ZB_ERR。 */
extern void   GwCoord_SendErr(uint8 code, uint8 ctx);

#ifdef __cplusplus
}
#endif

#endif /* __GW_COORD_H__ */
