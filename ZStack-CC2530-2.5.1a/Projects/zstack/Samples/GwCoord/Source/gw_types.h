#ifndef __GW_TYPES_H__
#define __GW_TYPES_H__

/*
 * CC2530 协调器共享常量（gateway_design_spec.md §4.5 / CC2530.md §4）。
 * 与 STM32 侧 App/config.h 保持一致。任何数值改动必须双端同步并同步规范书。
 */

#include "hal_types.h"   /* Z-Stack: uint8/uint16/uint32 */

/* ---------- UART 帧协议 ---------- */
#define FRAME_SOF1          0xAAu
#define FRAME_SOF2          0x55u
#define FRAME_EOF           0x0Du
#define FRAME_PAYLOAD_MAX   64u
#define FRAME_LEN_MIN       1u
#define FRAME_LEN_MAX       65u
#define FRAME_MAX           69u
#define FRAME_IDLE_MS       20u

/* ---------- CMD 表 ---------- */
#define CMD_ZB_PING         0x01u
#define CMD_ZB_NET_STATUS   0x02u
#define CMD_ZB_ALLOW_JOIN   0x10u
#define CMD_ZB_LIST_NODES   0x11u
#define CMD_ZB_NODE_INFO    0x12u
#define CMD_ZB_REPORT       0x20u
#define CMD_ZB_CMD          0x21u
#define CMD_ZB_ERR          0x7Fu
#define ZB_DEV_UNKNOWN      0x00u
#define ZB_DEV_ROUTER       0x01u
#define ZB_DEV_ENDDEV       0x02u
#define ZB_ROLE_UNKNOWN     0x00u
#define ZB_ROLE_TEMP_HUM    0x01u
#define ZB_ROLE_LUX         0x02u

/* ---------- dtype ---------- */
#define DTYPE_BOOL          0x01u
#define DTYPE_U8            0x02u
#define DTYPE_I8            0x03u
#define DTYPE_I16           0x10u
#define DTYPE_U16           0x11u
#define DTYPE_I32           0x12u
#define DTYPE_U32           0x20u
#define DTYPE_STRING        0x30u
#define DTYPE_BYTES         0x40u

/* ---------- ZB_ERR.code ---------- */
#define ERRC_LINK_DOWN      0x01u
#define ERRC_PANID_CONFLICT 0x02u
#define ERRC_NV_FAIL        0x03u
#define ERRC_SEND_FAIL      0x10u
#define ERRC_NODE_OFFLINE   0x11u
#define ERRC_QUEUE_FULL     0x12u
#define ERRC_BAD_FRAME      0x20u
#define ERRC_CRC_FAIL       0x21u
#define ERRC_UNKNOWN        0x7Fu

/* ---------- ZCL ---------- */
#define ZCL_CLU_TEMP        0x0402u
#define ZCL_CLU_HUM         0x0405u
#define ZCL_CLU_LUX         0x0400u
#define ZCL_CLU_ONOFF       0x0006u

/* ---------- 网络 ---------- */
#define ZB_CHANNEL_DEFAULT  15u
#define ZB_NODE_MAX         8u
#define ZB_HEARTBEAT_S      60u

/* ---------- OSAL 事件号（CC2530.md §5.6） ---------- */
#define GW_EVT_UART_RX      0x0001u
#define GW_EVT_HEARTBEAT    0x0002u
#define GW_EVT_JOIN_TMO     0x0004u

/* ---------- NV ID（CC2530.md §6） ---------- */
#define NV_ID_PANID         0xF001u
#define NV_ID_CHANNEL       0xF002u
#define NV_ID_NODE_TABLE    0xF003u

/* ---------- 返回码 ---------- */
#define RC_OK               0
#define RC_ERR_PARAM       -1
#define RC_ERR_NO_SPACE    -4
#define RC_ERR_BAD_FRAME   -5
#define RC_ERR_BAD_CRC     -6

/* ---------- 编译器适配 ---------- */
#if defined(__IAR_SYSTEMS_ICC__)
    #define CODE_QUAL  __code
    #define XDATA_QUAL __xdata
#elif defined(__SDCC)
    #define CODE_QUAL  __code
    #define XDATA_QUAL __xdata
#else
    #define CODE_QUAL
    #define XDATA_QUAL
#endif

#endif /* __GW_TYPES_H__ */
