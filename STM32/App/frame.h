#ifndef __PROTO_FRAME_H__
#define __PROTO_FRAME_H__

/*
 * UART 帧编解码（规范书 §4.5）：AA 55 LEN CMD PAYLOAD CRC8-ITU(0x07) 0D。
 * 与 CC2530 侧 Samples/GwCoord/Source/frame.{c,h} 同源；两处任何逻辑修改
 * 必须同时发生，并以规范书为唯一真相。
 */

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t cmd;
    uint8_t plen;                      /* 0..FRAME_PAYLOAD_MAX */
    uint8_t payload[FRAME_PAYLOAD_MAX];
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
    frame_state_t st;
    uint8_t  len;                      /* 报文 LEN 字节（CMD+PAYLOAD） */
    uint8_t  body_read;                /* 已读 body 字节数（不含 CMD） */
    uint8_t  crc_calc;                 /* 滑动 CRC8-ITU */
    uint32_t last_byte_ms;
    frame_t  acc;                      /* 正在拼装的帧 */
    /* 统计 */
    uint32_t frame_err;
    uint32_t crc_err;
    uint32_t ok_cnt;
} frame_parser_t;

/* CRC8-ITU：poly=0x07, init=0x00。输入长度 n，返回 CRC 值。 */
uint8_t crc8_itu(const uint8_t *data, uint32_t n);

/* 初始化/复位解析器。 */
void frame_parser_init(frame_parser_t *p);

/*
 * 喂一个字节；超过 FRAME_IDLE_MS 的静默会复位解析器。
 * 返回：1=收到完整帧已写 out；0=需要更多字节；<0=本次字节导致错误（解析器已复位）。
 */
int  frame_parser_feed(frame_parser_t *p, uint8_t b, uint32_t now_ms, frame_t *out);

/*
 * 把 cmd+payload 编码到 out_buf（buf_size 至少 FRAME_MAX）。
 * 返回：>0 = 整帧字节数；<0 = 参数错。
 */
int  frame_encode(uint8_t cmd, const uint8_t *payload, uint8_t plen,
                  uint8_t *out_buf, uint32_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_FRAME_H__ */
