/*
 * 构造最小 ZCL 帧并走 AF_DataRequest：
 *   Report Attributes (cmd=0x0A) / Default Response (cmd=0x0B)。
 * 不依赖 ZStack ZCL 库以减少初始化负担（规范书 §2.3 风险条目选择）。
 */

#include "zcl_report.h"
#include "AF.h"
#include "ZComDef.h"
#include "OSAL.h"

static uint8 s_zcl_seq = 0;

static uint8 dtype_to_vlen(uint8 dtype)
{
    switch (dtype) {
    case 0x01: case 0x02: case 0x03: return 1;   /* bool/u8/i8 */
    case 0x10: case 0x11:            return 2;   /* i16/u16 */
    case 0x12: case 0x20:            return 4;   /* i32/u32 */
    default:                         return 0;
    }
}

int8 zcl_report_send(uint8 src_ep, uint16 cluster, uint16 attrId,
                     uint8 dtype, const void *val, uint8 vlen)
{
    afAddrType_t dst;
    endPointDesc_t *ep_desc;
    uint8 buf[16];                    /* 3 头 + 2 attrId + 1 dtype + ≤4 val = 10B */
    uint8 *p;
    uint8 expect_vlen;
    uint8 rc;

    expect_vlen = dtype_to_vlen(dtype);
    if (expect_vlen == 0 || vlen != expect_vlen) return -1;
    if (val == 0) return -1;

    osal_memset(&dst, 0, sizeof(dst));
    dst.addrMode = (afAddrMode_t)AddrBroadcast;   /* 先用广播到 Coord（0xFFFC = routers+coord） */
    dst.addr.shortAddr = 0x0000;                  /* Coord 短地址 = 0 */
    dst.addrMode = (afAddrMode_t)Addr16Bit;
    dst.endPoint = 0x01;                           /* Coord EP1 */

    p = buf;
    *p++ = 0x18;                                   /* FC: cluster-spec | dir:server->client | disable default resp */
    *p++ = ++s_zcl_seq;
    *p++ = 0x0A;                                   /* Report Attributes */
    *p++ = (uint8)(attrId & 0xFF);
    *p++ = (uint8)(attrId >> 8);
    *p++ = dtype;
    osal_memcpy(p, val, vlen); p += vlen;

    ep_desc = afFindEndPointDesc(src_ep);
    if (ep_desc == 0) return -2;

    rc = AF_DataRequest(&dst, ep_desc, cluster,
                        (uint8)(p - buf), buf, &s_zcl_seq,
                        AF_DISCV_ROUTE, AF_DEFAULT_RADIUS);
    return (rc == afStatus_SUCCESS) ? 0 : -3;
}

int8 zcl_default_response(uint8 src_ep, uint16 cluster,
                          uint8 for_seq, uint8 for_cmd, uint8 status)
{
    afAddrType_t dst;
    endPointDesc_t *ep_desc;
    uint8 buf[5];
    uint8 rc;

    osal_memset(&dst, 0, sizeof(dst));
    dst.addrMode = (afAddrMode_t)Addr16Bit;
    dst.addr.shortAddr = 0x0000;
    dst.endPoint = 0x01;

    buf[0] = 0x18;                                 /* FC: cluster-spec | s→c | no default */
    buf[1] = for_seq;
    buf[2] = 0x0B;                                 /* Default Response */
    buf[3] = for_cmd;
    buf[4] = status;

    ep_desc = afFindEndPointDesc(src_ep);
    if (ep_desc == 0) return -2;

    rc = AF_DataRequest(&dst, ep_desc, cluster,
                        sizeof(buf), buf, &for_seq,
                        AF_DISCV_ROUTE, AF_DEFAULT_RADIUS);
    return (rc == afStatus_SUCCESS) ? 0 : -3;
}
