/*
 * Ai-WB2-01S WiFi TCP 客户端透传驱动。
 *
 * 状态机：
 *   IDLE (EN=1, 等模组上电 boot log)
 *     -> WMODE     AT+WMODE=1,1                 等 "OK"
 *     -> WJAP      AT+WJAP="SSID","PWD"         等 "WIFI_CONNECT"
 *     -> SOCKET    AT+SOCKET=4,"IP",PORT        等 "OK"
 *     -> SOCKETTT  AT+SOCKETTT                  等 ">"
 *     -> TRANSPARENT
 *   任一步超时 -> BACKOFF (2/4/8/16 s) -> 回 IDLE (先 EN=0 复位模组)
 *
 * 非透传态：按 '\r' / '\n' 切 AT 响应行，strstr 匹配关键字推进。
 * 透传态：按 '\n' / '\r' 切 JSON 行，回调 s_cb。
 */

#include "wifi_link.h"
#include "drv_usart.h"
#include "drv_gpio.h"
#include "drv_tim.h"
#include "bsp_log.h"
#include "config.h"
#include "err.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    W_IDLE = 0,
    W_ENABLE,
    W_WMODE,
    W_WJAP,
    W_SOCKET,
    W_SOCKETTT,
    W_TRANSPARENT,
    W_BACKOFF
} wifi_st_t;

static wifi_st_t  s_st = W_IDLE;
static uint32_t   s_st_ts = 0;
static uint32_t   s_at_deadline = 0;
static uint32_t   s_backoff_s = 2;
static wifi_line_cb_t s_cb = 0;

/* 透传态的 JSON 行缓冲（>=1 行） */
#define LINE_BUF_SZ   JSON_LINE_MAX
static char       s_line[LINE_BUF_SZ];
static uint16_t   s_line_len = 0;

/* AT 响应行缓冲（较短够用） */
#define AT_BUF_SZ     128u
static char       s_at_line[AT_BUF_SZ];
static uint16_t   s_at_len = 0;
static uint8_t    s_at_hit = 0;   /* 本步关键字是否已命中 */

static void send_at(const char *s)
{
    wifi_uart_write(s, (uint16_t)strlen(s));
    wifi_uart_write("\r\n", 2);
}

static void enter(wifi_st_t st, uint32_t now_ms, uint32_t timeout_ms)
{
    s_st = st;
    s_st_ts = now_ms;
    s_at_deadline = now_ms + timeout_ms;
    s_at_len = 0;
    s_at_hit = 0;
    LOGI("wifi", "state=%d", (int)st);
}

static void enter_backoff(uint32_t now_ms)
{
    wifi_en_set(0);
    s_st = W_BACKOFF;
    s_st_ts = now_ms;
    LOGW("wifi", "backoff=%us", (unsigned)s_backoff_s);
}

void wifi_link_init(wifi_line_cb_t cb)
{
    s_cb = cb;
    s_st = W_IDLE;
    s_line_len = 0;
    s_at_len = 0;
    s_at_hit = 0;
    s_backoff_s = 2;
}

uint8_t wifi_link_is_up(void) { return s_st == W_TRANSPARENT; }

int wifi_link_send_line(const char *line, uint16_t n)
{
    if (s_st != W_TRANSPARENT) return RC_ERR_BUSY;
    wifi_uart_write(line, n);
    if (n == 0 || line[n - 1] != '\n') wifi_uart_write("\n", 1);
    return 0;
}

/* 当前状态期望的关键字；返回 1 表示命中可推进。 */
static uint8_t at_line_match(void)
{
    switch (s_st) {
    case W_WMODE:
    case W_SOCKET:
        return strstr(s_at_line, "OK") != 0;
    case W_WJAP:
        return strstr(s_at_line, "WIFI_CONNECT") != 0 ||
               strstr(s_at_line, "OK") != 0;
    case W_SOCKETTT:
        return strstr(s_at_line, ">") != 0 ||
               strstr(s_at_line, "OK") != 0;
    default:
        return 0;
    }
}

static void feed_at_byte(uint8_t b)
{
    if (b == '\r' || b == '\n') {
        if (s_at_len > 0) {
            s_at_line[s_at_len] = '\0';
            if (at_line_match()) s_at_hit = 1;
            s_at_len = 0;
        }
    } else if (s_at_len + 1 < AT_BUF_SZ) {
        s_at_line[s_at_len++] = (char)b;
        /* 部分关键字（例如 '>'）不以换行结尾，边攒边查 */
        if (s_st == W_SOCKETTT && b == '>') {
            s_at_line[s_at_len] = '\0';
            s_at_hit = 1;
        }
    } else {
        s_at_len = 0;    /* 超长丢弃 */
    }
}

void wifi_link_poll(uint32_t now_ms)
{
    uint8_t b;
    while (wifi_uart_rx_available()) {
        if (wifi_uart_read(&b, 1) == 0) break;
        if (s_st == W_TRANSPARENT) {
            if (b == '\n' || b == '\r') {
                if (s_line_len > 0 && s_cb) s_cb(s_line, s_line_len);
                s_line_len = 0;
            } else if (s_line_len + 1 < LINE_BUF_SZ) {
                s_line[s_line_len++] = (char)b;
            } else {
                s_line_len = 0;
            }
        } else {
            feed_at_byte(b);
        }
    }
    (void)now_ms;
}

void wifi_link_tick(uint32_t now_ms)
{
    char cmd[96];

    switch (s_st) {
    case W_IDLE:
        wifi_en_set(1);
        enter(W_ENABLE, now_ms, 500);   /* 等模组 boot */
        break;

    case W_ENABLE:
        if (now_ms >= s_at_deadline) {
            send_at("AT+WMODE=1,1");
            enter(W_WMODE, now_ms, 3000);
        }
        break;

    case W_WMODE:
        if (s_at_hit) {
            snprintf(cmd, sizeof(cmd), "AT+WJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PWD);
            send_at(cmd);
            enter(W_WJAP, now_ms, 15000);
        } else if (now_ms >= s_at_deadline) {
            enter_backoff(now_ms);
        }
        break;

    case W_WJAP:
        if (s_at_hit) {
            snprintf(cmd, sizeof(cmd), "AT+SOCKET=4,\"%s\",%u",
                     SERVER_IP, (unsigned)SERVER_PORT);
            send_at(cmd);
            enter(W_SOCKET, now_ms, 5000);
        } else if (now_ms >= s_at_deadline) {
            enter_backoff(now_ms);
        }
        break;

    case W_SOCKET:
        if (s_at_hit) {
            send_at("AT+SOCKETTT");
            enter(W_SOCKETTT, now_ms, 3000);
        } else if (now_ms >= s_at_deadline) {
            enter_backoff(now_ms);
        }
        break;

    case W_SOCKETTT:
        if (s_at_hit) {
            enter(W_TRANSPARENT, now_ms, 0);
            s_backoff_s = 2;    /* 成功后重置退避窗 */
        } else if (now_ms >= s_at_deadline) {
            enter_backoff(now_ms);
        }
        break;

    case W_TRANSPARENT:
        /* TODO: 连接丢失检测（心跳超时回环）留给上层，本层常连。 */
        break;

    case W_BACKOFF:
        if (now_ms - s_st_ts >= (uint32_t)s_backoff_s * 1000u) {
            if (s_backoff_s < 16) s_backoff_s *= 2;
            s_st = W_IDLE;   /* 下个 tick 再 EN=1 */
        }
        break;
    }
}
