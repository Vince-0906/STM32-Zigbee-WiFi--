#ifndef __CMD_MAP_H__
#define __CMD_MAP_H__

#include "gw_types.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 分发一帧；未知 CMD 将回 ZB_ERR{UNKNOWN,cmd}。 */
void cmd_map_dispatch(const frame_t *f);

#ifdef __cplusplus
}
#endif

#endif /* __CMD_MAP_H__ */
