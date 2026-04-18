#ifndef __FRAME_H__
#define __FRAME_H__

/*
 * UART 帧编解码（CC2530 侧，与 STM32 App/proto/frame.{c,h} 同源）。
 * 协议见 gateway_design_spec.md §4.5 / CC2530.md §4.1。
 */

#include "gw_types.h"

typedef struct {
    uint8 cmd;
    uint8 plen;
    uint8 payload[FRAME_PAYLOAD_MAX];
} frame_t;

typedef enum {
    FP_S_SOF1 = 0,
    FP_S_SOF2,
    FP_S_LEN,
    FP_S_CMD,
    FP_S_BODY,
    FP_S_CRC,
    FP_S_EOF
} frame_state_t;

typedef struct {
    uint8   st;             /* frame_state_t，存为 uint8 便于 8051 xdata 对齐 */
    uint8   len;
    uint8   body_read;
    uint8   crc_calc;
    uint32  last_byte_ms;
    frame_t acc;
    uint32  frame_err;
    uint32  crc_err;
    uint32  ok_cnt;
} frame_parser_t;

uint8 crc8_itu(const uint8 *data, uint16 n);

void  frame_parser_init(frame_parser_t XDATA_QUAL *p);
int8  frame_parser_feed(frame_parser_t XDATA_QUAL *p, uint8 b,
                        uint32 now_ms, frame_t XDATA_QUAL *out);

/* 返回整帧字节数（>0） or 负错误码。 */
int16 frame_encode(uint8 cmd, const uint8 *payload, uint8 plen,
                   uint8 *out_buf, uint16 buf_size);

#endif /* __FRAME_H__ */
