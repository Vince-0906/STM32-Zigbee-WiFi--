#ifndef __GW_COORD_H__
#define __GW_COORD_H__

#include "ZComDef.h"
#include "gw_types.h"
#include "frame.h"
#include "zb_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GWCOORD_ENDPOINT        0x01u
#define GWCOORD_PROFILE_ID      0x0104u
#define GWCOORD_DEVICE_ID       0x0000u
#define GWCOORD_DEVICE_VERSION  1
#define GWCOORD_FLAGS           0
#define GWCOORD_CLUSTERS_MAX    4u

extern uint8 GwCoord_TaskID;

void   GwCoord_Init(uint8 task_id);
UINT16 GwCoord_ProcessEvent(uint8 task_id, uint16 events);
void   GwCoord_OnFrame(const frame_t *f);
int8   GwCoord_SendFrame(uint8 cmd, const uint8 *payload, uint8 plen);
void   GwCoord_SendErr(uint8 code, uint8 ctx);
void   GwCoord_SendNodeInfo(const zb_node_t *node);
void   GwCoord_SendNetStatus(void);
void   GwCoord_RefreshLocalDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* __GW_COORD_H__ */
