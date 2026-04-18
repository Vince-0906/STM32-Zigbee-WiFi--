/*
 * ZB 链路服务：驱动 frame_parser；10s ZB_PING 保活；3 次无回 → 硬复位 CC2530。
 */

#include "zb_link.h"
#include "drv_usart.h"
#include "drv_gpio.h"
#include "drv_tim.h"
#include "bsp_log.h"
#include "config.h"
#include "err.h"

static frame_parser_t s_parser;
static zb_link_on_frame_t s_cb = 0;

static uint32_t s_last_ping_ms = 0;
static uint32_t s_last_rx_ms   = 0;
static uint8_t  s_miss_cnt     = 0;
static uint8_t  s_alive        = 0;
static uint32_t s_ok = 0, s_err = 0;

static uint8_t s_tx_buf[FRAME_MAX];

void zb_link_init(zb_link_on_frame_t cb)
{
    frame_parser_init(&s_parser);
    s_cb = cb;
    s_last_ping_ms = 0;
    s_last_rx_ms = 0;
    s_miss_cnt = 0;
    s_alive = 0;
}

static void on_frame(const frame_t *f)
{
    s_last_rx_ms = ms_now();
    s_miss_cnt = 0;
    s_alive = 1;
    s_ok++;
    if (f->cmd == CMD_ZB_PING) {
        /* 保活应答：不再上交业务层 */
        return;
    }
    if (s_cb) s_cb(f);
}

void zb_link_poll(uint32_t now_ms)
{
    uint8_t b;
    frame_t out;
    int rc;
    while (zb_uart_rx_available()) {
        if (zb_uart_read(&b, 1) == 0) break;
        rc = frame_parser_feed(&s_parser, b, now_ms, &out);
        if (rc == 1) {
            on_frame(&out);
        } else if (rc < 0) {
            s_err++;
        }
    }
}

int zb_link_send(uint8_t cmd, const uint8_t *payload, uint8_t plen)
{
    int n = frame_encode(cmd, payload, plen, s_tx_buf, sizeof(s_tx_buf));
    if (n <= 0) return n;
    return zb_uart_write(s_tx_buf, (uint16_t)n) == n ? 0 : RC_ERR_IO;
}

void zb_link_tick(uint32_t now_ms)
{
    /* 心跳 PING */
    if (now_ms - s_last_ping_ms >= ZB_HEARTBEAT_MS) {
        s_last_ping_ms = now_ms;
        (void)zb_link_send(CMD_ZB_PING, 0, 0);
        /* 如果自上次收到 > 心跳窗口，计一次 miss */
        if (s_last_rx_ms != 0 && (now_ms - s_last_rx_ms) > ZB_HEARTBEAT_MS) {
            if (s_miss_cnt < 255) s_miss_cnt++;
            if (s_miss_cnt >= ZB_HEARTBEAT_FAILS) {
                LOGW("zb", "heartbeat lost, resetting CC2530");
                cc2530_reset_pulse();
                s_miss_cnt = 0;
                s_alive = 0;
                s_last_rx_ms = now_ms;
            }
        }
    }
}

uint32_t zb_link_ok_cnt(void)  { return s_ok; }
uint32_t zb_link_err_cnt(void) { return s_err; }
uint8_t  zb_link_alive(void)   { return s_alive; }
