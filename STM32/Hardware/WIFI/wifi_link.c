/*
 * Ai-WB2-01S WiFi TCP client transparent-link driver.
 *
 * State machine:
 *   IDLE -> ENABLE -> WMODE -> WJAP -> SOCKET -> SOCKETTT -> TRANSPARENT
 * Any AT-stage timeout or explicit error falls back to BACKOFF and retries
 * with exponential backoff. Once transparent mode is up, downlink silence alone
 * is not treated as a dead connection.
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
    W_EXIT_TT,
    W_SOCKETDEL,
    W_BACKOFF
} wifi_st_t;

static wifi_st_t      s_st = W_IDLE;
static uint32_t       s_st_ts = 0;
static uint32_t       s_at_deadline = 0;
static uint32_t       s_backoff_s = 2;
static wifi_line_cb_t s_cb = 0;

#define LINE_BUF_SZ   JSON_LINE_MAX
static char           s_line[LINE_BUF_SZ];
static uint16_t       s_line_len = 0;

#define AT_BUF_SZ     128u
static char           s_at_line[AT_BUF_SZ];
static uint16_t       s_at_len = 0;
static uint8_t        s_at_hit = 0;
static uint8_t        s_at_fail = 0;
static uint8_t        s_at_seen = 0;
static char           s_last_at_line[AT_BUF_SZ];
static char           s_last_tx_line[AT_BUF_SZ];
static uint8_t        s_wifi_got_ip_seen = 0;
static uint8_t        s_socket_connect_seen = 0;
static uint8_t        s_socket_conid = 0;

static uint8_t at_line_match(void)
{
    switch (s_st) {
    case W_WMODE:
        return strstr(s_at_line, "OK") != 0;
    case W_WJAP:
        return s_wifi_got_ip_seen;
    case W_SOCKET:
        return s_socket_connect_seen;
    case W_SOCKETTT:
        return strstr(s_at_line, ">") != 0;
    case W_EXIT_TT:
    case W_SOCKETDEL:
        return strstr(s_at_line, "OK") != 0;
    default:
        return 0;
    }
}

static uint8_t at_line_failed(const char *line)
{
    return (strstr(line, "ERROR") != 0) ||
           (strstr(line, "FAIL") != 0) ||
           (strstr(line, "fail") != 0) ||
           (strstr(line, "busy") != 0);
}

static void remember_tx_line(const char *s)
{
    snprintf(s_last_tx_line, sizeof(s_last_tx_line), "%s", s);
    LOGI("wifi", "at> %s", s_last_tx_line);
}

static void remember_rx_line(const char *s)
{
    snprintf(s_last_at_line, sizeof(s_last_at_line), "%s", s);
    s_at_seen = 1u;
    LOGI("wifi", "at< %s", s_last_at_line);
    if (strstr(s_last_at_line, "+EVENT:WIFI_GOT_IP") != 0) {
        s_wifi_got_ip_seen = 1u;
    }
    if (strstr(s_last_at_line, "connect success ConID=") != 0) {
        unsigned conid = 0u;

        if (sscanf(s_last_at_line, "connect success ConID=%u", &conid) == 1) {
            s_socket_conid = (uint8_t)conid;
        } else {
            s_socket_conid = 1u;
        }
        s_socket_connect_seen = 1u;
    }
    if (at_line_failed(s_last_at_line)) {
        s_at_fail = 1u;
    }
}

static void log_timeout_detail(void)
{
    if (s_last_tx_line[0] != '\0') {
        LOGW("wifi", "last_tx=%s", s_last_tx_line);
    }
    if (s_at_seen) {
        LOGW("wifi", "last_rx=%s", s_last_at_line);
    } else {
        LOGW("wifi", "last_rx=<none>");
    }
}

static void send_at(const char *s)
{
    remember_tx_line(s);
    wifi_uart_write(s, (uint16_t)strlen(s));
    wifi_uart_write("\r\n", 2u);
}

static void send_tt_exit(void)
{
    remember_tx_line("+++");
    wifi_uart_write("+++", 3u);
}

static void enter(wifi_st_t st, uint32_t now_ms, uint32_t timeout_ms)
{
    s_st = st;
    s_st_ts = now_ms;
    s_at_deadline = now_ms + timeout_ms;
    s_at_len = 0u;
    s_at_hit = 0u;
    s_at_fail = 0u;
    s_at_seen = 0u;
    s_last_at_line[0] = '\0';

    if (st == W_WJAP) {
        s_wifi_got_ip_seen = 0u;
    } else if (st == W_SOCKET) {
        s_socket_connect_seen = 0u;
        s_socket_conid = 0u;
    }
    LOGI("wifi", "state=%d", (int)st);
}

static void start_backoff(uint32_t now_ms)
{
    wifi_en_set(0);
    s_st = W_BACKOFF;
    s_st_ts = now_ms;
    LOGW("wifi", "backoff=%us", (unsigned)s_backoff_s);
}

static void enter_backoff(uint32_t now_ms)
{
    char cmd[32];

    if (s_st == W_TRANSPARENT) {
        send_tt_exit();
        enter(W_EXIT_TT, now_ms, 1000u);
        return;
    }

    if (s_st == W_EXIT_TT && s_socket_conid != 0u) {
        snprintf(cmd, sizeof(cmd), "AT+SOCKETDEL=%u", (unsigned)s_socket_conid);
        send_at(cmd);
        enter(W_SOCKETDEL, now_ms, 1000u);
        return;
    }

    start_backoff(now_ms);
}

void wifi_link_init(wifi_line_cb_t cb)
{
    s_cb = cb;
    s_st = W_IDLE;
    s_line_len = 0u;
    s_at_len = 0u;
    s_at_hit = 0u;
    s_at_fail = 0u;
    s_at_seen = 0u;
    s_last_at_line[0] = '\0';
    s_last_tx_line[0] = '\0';
    s_wifi_got_ip_seen = 0u;
    s_socket_connect_seen = 0u;
    s_socket_conid = 0u;
    s_backoff_s = 2u;
}

uint8_t wifi_link_is_up(void)
{
    return s_st == W_TRANSPARENT;
}

int wifi_link_send_line(const char *line, uint16_t n)
{
    if (s_st != W_TRANSPARENT) {
        return RC_ERR_BUSY;
    }
    wifi_uart_write(line, n);
    if (n == 0u || line[n - 1u] != '\n') {
        wifi_uart_write("\n", 1u);
    }
    return 0;
}

static void feed_at_byte(uint8_t b)
{
    if (b == '\r' || b == '\n') {
        if (s_at_len > 0u) {
            s_at_line[s_at_len] = '\0';
            remember_rx_line(s_at_line);
            if (at_line_match()) {
                s_at_hit = 1u;
            }
            s_at_len = 0u;
        }
    } else if (s_at_len + 1u < AT_BUF_SZ) {
        s_at_line[s_at_len++] = (char)b;
        if (s_st == W_SOCKETTT && b == '>') {
            s_at_line[s_at_len] = '\0';
            remember_rx_line(s_at_line);
            s_at_hit = 1u;
        }
    } else {
        s_at_len = 0u;
    }
}

void wifi_link_poll(uint32_t now_ms)
{
    uint8_t b;

    while (wifi_uart_rx_available()) {
        if (wifi_uart_read(&b, 1u) == 0) {
            break;
        }
        if (s_st == W_TRANSPARENT) {
            if (b == '\n' || b == '\r') {
                if (s_line_len > 0u && s_cb) {
                    s_cb(s_line, s_line_len);
                }
                s_line_len = 0u;
            } else if (s_line_len + 1u < LINE_BUF_SZ) {
                s_line[s_line_len++] = (char)b;
            } else {
                s_line_len = 0u;
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
        enter(W_ENABLE, now_ms, 500u);
        break;

    case W_ENABLE:
        if (now_ms >= s_at_deadline) {
            send_at("AT+WMODE=1,1");
            enter(W_WMODE, now_ms, 3000u);
        }
        break;

    case W_WMODE:
        if (s_at_hit) {
            snprintf(cmd, sizeof(cmd), "AT+WJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PWD);
            send_at(cmd);
            enter(W_WJAP, now_ms, 15000u);
        } else if (s_at_fail) {
            log_timeout_detail();
            enter_backoff(now_ms);
        } else if (now_ms >= s_at_deadline) {
            log_timeout_detail();
            enter_backoff(now_ms);
        }
        break;

    case W_WJAP:
        if (s_at_hit) {
            snprintf(cmd, sizeof(cmd), "AT+SOCKET=4,%s,%u",
                     SERVER_IP, (unsigned)SERVER_PORT);
            send_at(cmd);
            enter(W_SOCKET, now_ms, 5000u);
        } else if (s_at_fail) {
            log_timeout_detail();
            enter_backoff(now_ms);
        } else if (now_ms >= s_at_deadline) {
            log_timeout_detail();
            enter_backoff(now_ms);
        }
        break;

    case W_SOCKET:
        if (s_at_hit) {
            send_at("AT+SOCKETTT");
            enter(W_SOCKETTT, now_ms, 3000u);
        } else if (s_at_fail) {
            log_timeout_detail();
            enter_backoff(now_ms);
        } else if (now_ms >= s_at_deadline) {
            log_timeout_detail();
            enter_backoff(now_ms);
        }
        break;

    case W_SOCKETTT:
        if (s_at_hit) {
            enter(W_TRANSPARENT, now_ms, 0u);
            s_backoff_s = 2u;
        } else if (s_at_fail) {
            log_timeout_detail();
            enter_backoff(now_ms);
        } else if (now_ms >= s_at_deadline) {
            log_timeout_detail();
            enter_backoff(now_ms);
        }
        break;

    case W_TRANSPARENT:
        break;

    case W_EXIT_TT:
        if (s_at_hit) {
            enter_backoff(now_ms);
        } else if (s_at_fail || now_ms >= s_at_deadline) {
            log_timeout_detail();
            start_backoff(now_ms);
        }
        break;

    case W_SOCKETDEL:
        if (s_at_hit) {
            s_socket_conid = 0u;
            start_backoff(now_ms);
        } else if (s_at_fail || now_ms >= s_at_deadline) {
            log_timeout_detail();
            start_backoff(now_ms);
        }
        break;

    case W_BACKOFF:
        if ((now_ms - s_st_ts) >= (uint32_t)s_backoff_s * 1000u) {
            if (s_backoff_s < 16u) {
                s_backoff_s *= 2u;
            }
            s_st = W_IDLE;
        }
        break;
    }
}
