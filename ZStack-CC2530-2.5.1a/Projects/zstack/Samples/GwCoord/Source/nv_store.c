/*
 * NV 持久化薄封装（CC2530.md §6）。
 * - osal_nv_item_init + osal_nv_read/osal_nv_write。
 * - 只允许固定 id 集：0xF001 panid / 0xF002 channel / 0xF003 节点表（预留）。
 */

#include "nv_store.h"
#include "OSAL_Nv.h"

int8 nv_load_u16(uint16 id, uint16 *val)
{
    uint8 st;
    if (val == 0) return (int8)RC_ERR_PARAM;
    st = osal_nv_item_init(id, sizeof(uint16), val);
    if (st == SUCCESS || st == NV_ITEM_UNINIT) {
        if (osal_nv_read(id, 0, sizeof(uint16), val) == SUCCESS) return 0;
    }
    return -1;
}

int8 nv_save_u16(uint16 id, uint16 val)
{
    uint8 st;
    st = osal_nv_item_init(id, sizeof(uint16), &val);
    (void)st;
    return (osal_nv_write(id, 0, sizeof(uint16), &val) == SUCCESS) ? 0 : -1;
}

int8 nv_load_u8(uint16 id, uint8 *val)
{
    uint8 st;
    if (val == 0) return (int8)RC_ERR_PARAM;
    st = osal_nv_item_init(id, sizeof(uint8), val);
    if (st == SUCCESS || st == NV_ITEM_UNINIT) {
        if (osal_nv_read(id, 0, sizeof(uint8), val) == SUCCESS) return 0;
    }
    return -1;
}

int8 nv_save_u8(uint16 id, uint8 val)
{
    uint8 st;
    st = osal_nv_item_init(id, sizeof(uint8), &val);
    (void)st;
    return (osal_nv_write(id, 0, sizeof(uint8), &val) == SUCCESS) ? 0 : -1;
}
