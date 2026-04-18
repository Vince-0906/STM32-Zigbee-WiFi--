/*
 * UART 帧编解码实现（规范书 §4.5）。CRC8-ITU poly=0x07/init=0x00，
 * 在 [CMD..PAYLOAD 末] 上滚动计算。
 *
 * 同源文件：D:\ZNJJ\ZStack-CC2530-2.5.1a\Projects\zstack\Samples\GwCoord\Source\frame.c
 * 任意改动必须双端同步。
 */

#include "frame.h"
#include "err.h"

/* 预计算表：CRC8-ITU, reflected=false, poly=0x07, init=0x00。 */
static const uint8_t CRC8_ITU_TAB[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3
};

uint8_t crc8_itu(const uint8_t *data, uint32_t n)
{
    uint8_t c = 0x00;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        c = CRC8_ITU_TAB[c ^ data[i]];
    }
    return c;
}

void frame_parser_init(frame_parser_t *p)
{
    p->st = FP_S_SOF1;
    p->len = 0;
    p->body_read = 0;
    p->crc_calc = 0;
    p->last_byte_ms = 0;
    p->acc.cmd = 0;
    p->acc.plen = 0;
    p->frame_err = 0;
    p->crc_err = 0;
    p->ok_cnt = 0;
}

static void parser_soft_reset(frame_parser_t *p)
{
    p->st = FP_S_SOF1;
    p->body_read = 0;
    p->crc_calc = 0;
    p->len = 0;
}

int frame_parser_feed(frame_parser_t *p, uint8_t b, uint32_t now_ms, frame_t *out)
{
    /* 字节间隔 >20 ms → 强制复位。 */
    if (p->st != FP_S_SOF1 && (now_ms - p->last_byte_ms) > FRAME_IDLE_MS) {
        parser_soft_reset(p);
        p->frame_err++;
    }
    p->last_byte_ms = now_ms;

    switch (p->st) {
    case FP_S_SOF1:
        if (b == FRAME_SOF1) { p->st = FP_S_SOF2; }
        return 0;

    case FP_S_SOF2:
        if (b == FRAME_SOF2) {
            p->st = FP_S_LEN;
        } else if (b == FRAME_SOF1) {
            /* 保持在 SOF2 等待，对 AA AA 55 ... 情况容忍 */
        } else {
            parser_soft_reset(p);
            p->frame_err++;
            return RC_ERR_BAD_FRAME;
        }
        return 0;

    case FP_S_LEN:
        if (b < FRAME_LEN_MIN || b > FRAME_LEN_MAX) {
            parser_soft_reset(p);
            p->frame_err++;
            return RC_ERR_BAD_FRAME;
        }
        p->len = b;
        p->crc_calc = 0;
        p->body_read = 0;
        p->st = FP_S_CMD;
        return 0;

    case FP_S_CMD:
        p->acc.cmd = b;
        p->crc_calc = CRC8_ITU_TAB[p->crc_calc ^ b];
        p->acc.plen = (uint8_t)(p->len - 1);
        if (p->acc.plen == 0) {
            p->st = FP_S_CRC;
        } else {
            p->st = FP_S_BODY;
        }
        return 0;

    case FP_S_BODY:
        p->acc.payload[p->body_read++] = b;
        p->crc_calc = CRC8_ITU_TAB[p->crc_calc ^ b];
        if (p->body_read >= p->acc.plen) {
            p->st = FP_S_CRC;
        }
        return 0;

    case FP_S_CRC:
        if (b != p->crc_calc) {
            parser_soft_reset(p);
            p->crc_err++;
            return RC_ERR_BAD_CRC;
        }
        p->st = FP_S_EOF;
        return 0;

    case FP_S_EOF:
        if (b == FRAME_EOF) {
            *out = p->acc;
            parser_soft_reset(p);
            p->ok_cnt++;
            return 1;
        } else {
            parser_soft_reset(p);
            p->frame_err++;
            return RC_ERR_BAD_FRAME;
        }

    default:
        parser_soft_reset(p);
        return RC_ERR_BAD_FRAME;
    }
}

int frame_encode(uint8_t cmd, const uint8_t *payload, uint8_t plen,
                 uint8_t *out_buf, uint32_t buf_size)
{
    uint32_t total;
    uint8_t  crc;
    uint8_t  i;

    if (out_buf == 0) return RC_ERR_PARAM;
    if (plen > FRAME_PAYLOAD_MAX) return RC_ERR_PARAM;
    if (plen > 0 && payload == 0) return RC_ERR_PARAM;

    total = (uint32_t)(2 + 1 + 1 + plen + 1 + 1); /* SOF1 SOF2 LEN CMD payload CRC EOF */
    if (buf_size < total) return RC_ERR_NO_SPACE;

    out_buf[0] = FRAME_SOF1;
    out_buf[1] = FRAME_SOF2;
    out_buf[2] = (uint8_t)(1 + plen);
    out_buf[3] = cmd;
    for (i = 0; i < plen; ++i) {
        out_buf[4 + i] = payload[i];
    }
    /* CRC 覆盖 CMD + payload */
    crc = 0x00;
    crc = CRC8_ITU_TAB[crc ^ cmd];
    for (i = 0; i < plen; ++i) {
        crc = CRC8_ITU_TAB[crc ^ payload[i]];
    }
    out_buf[4 + plen] = crc;
    out_buf[5 + plen] = FRAME_EOF;
    return (int)total;
}

/* ---------- Host 测试入口（PC 上 gcc -DHOST_TEST 编译） ---------- */
#ifdef HOST_TEST
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void dump(const char *tag, const uint8_t *b, uint32_t n) {
    printf("%s[%u]:", tag, n);
    for (uint32_t i = 0; i < n; ++i) printf(" %02X", b[i]);
    printf("\n");
}

int main(void) {
    /* 1) 空 payload PING：AA 55 01 01 <crc> 0D */
    uint8_t buf[FRAME_MAX];
    int n = frame_encode(CMD_ZB_PING, 0, 0, buf, sizeof(buf));
    assert(n == 6);
    assert(buf[0]==0xAA && buf[1]==0x55 && buf[2]==0x01 && buf[3]==0x01 && buf[5]==0x0D);
    uint8_t crc_expected = crc8_itu((uint8_t[]){0x01}, 1);
    assert(buf[4] == crc_expected);
    dump("PING", buf, n);

    /* 2) ZB_REPORT: nodeid=1,ep=1,cluster=0x0402,dtype=0x10,vlen=2,val=0x0A50
     *    payload: 01 00 01 02 04 10 02 50 0A  (9B), LEN = 1+9=10 */
    uint8_t pl[] = {0x01,0x00, 0x01, 0x02,0x04, 0x10, 0x02, 0x50,0x0A};
    n = frame_encode(CMD_ZB_REPORT, pl, sizeof(pl), buf, sizeof(buf));
    assert(n == 2 + 1 + 1 + (int)sizeof(pl) + 1 + 1);
    assert(buf[2] == 1 + (uint8_t)sizeof(pl));
    dump("REPORT", buf, n);

    /* 3) Roundtrip：解析自己编的帧 */
    frame_parser_t p;
    frame_parser_init(&p);
    frame_t got;
    int rc = 0, hits = 0;
    for (int i = 0; i < n; ++i) {
        rc = frame_parser_feed(&p, buf[i], (uint32_t)i, &got);
        if (rc == 1) hits++;
    }
    assert(hits == 1);
    assert(got.cmd == CMD_ZB_REPORT);
    assert(got.plen == sizeof(pl));
    assert(memcmp(got.payload, pl, sizeof(pl)) == 0);

    /* 4) 破坏 CRC → 预期 RC_ERR_BAD_CRC */
    buf[4 + sizeof(pl)] ^= 0xFF;
    frame_parser_init(&p);
    int saw_crc_err = 0;
    for (int i = 0; i < n; ++i) {
        rc = frame_parser_feed(&p, buf[i], (uint32_t)i, &got);
        if (rc == RC_ERR_BAD_CRC) saw_crc_err = 1;
    }
    assert(saw_crc_err);

    /* 5) 字节间隔超时 → parser 复位 */
    frame_parser_init(&p);
    frame_parser_feed(&p, 0xAA, 0, &got);
    frame_parser_feed(&p, 0x55, 1000 /* 远超 20ms */, &got);
    /* 状态应回到 SOF1 并且 frame_err 增加 */
    assert(p.frame_err > 0);

    printf("frame.c HOST_TEST: ALL OK (ok=%u, crc_err=%u, frame_err=%u)\n",
           p.ok_cnt, p.crc_err, p.frame_err);
    return 0;
}
#endif /* HOST_TEST */
