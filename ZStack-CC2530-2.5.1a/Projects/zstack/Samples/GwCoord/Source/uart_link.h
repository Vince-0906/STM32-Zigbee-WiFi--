#ifndef __UART_LINK_H__
#define __UART_LINK_H__

/*
 * CC2530 UART 链路层：HalUART 回调拷字节 → 任务内驱动 frame_parser。
 */

#include "gw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void  uart_link_init(uint8 owner_task_id);

/*
 * 在 GwCoord_ProcessEvent 收到 GW_EVT_UART_RX 后调用：
 * 从环形缓冲消耗字节并驱动 frame 解析，遇完整帧回调 GwCoord_OnFrame。
 */
void  uart_link_drain(void);

/* 原始发送（阻塞入 UART tx）。0=OK, <0=err。 */
int8  uart_link_tx(const uint8 *data, uint16 n);

/* 统计访问器 */
uint32 uart_link_frame_err(void);
uint32 uart_link_crc_err(void);
uint32 uart_link_ok_cnt(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_LINK_H__ */
