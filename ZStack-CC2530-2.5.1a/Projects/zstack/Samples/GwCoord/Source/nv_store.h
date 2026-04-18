#ifndef __NV_STORE_H__
#define __NV_STORE_H__

#include "gw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int8 nv_load_u16(uint16 id, uint16 *val);
int8 nv_save_u16(uint16 id, uint16 val);
int8 nv_load_u8 (uint16 id, uint8  *val);
int8 nv_save_u8 (uint16 id, uint8  val);

#ifdef __cplusplus
}
#endif

#endif /* __NV_STORE_H__ */
