#ifndef __ZCL_REPORT_H__
#define __ZCL_REPORT_H__

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 构造 ZCL Report Attributes 帧并走 AF_DataRequest。
 *  dtype 复用规范书 §4.5.3（与 ZCL data type 兼容）。
 *  返回 0=OK, <0=err。
 */
int8 zcl_report_send(uint8 src_ep, uint16 cluster, uint16 attrId,
                     uint8 dtype, const void *val, uint8 vlen);

/* 发送 Default Response（用于对 OnOff 命令回应）。 */
int8 zcl_default_response(uint8 src_ep, uint16 cluster,
                          uint8 for_seq, uint8 for_cmd, uint8 status);

#ifdef __cplusplus
}
#endif

#endif /* __ZCL_REPORT_H__ */
