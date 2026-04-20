/*
 * 主事件循环（规范书 §5.2）。
 */

#include "gw_main.h"
#include "router.h"
#include "automation.h"
#include "config.h"
#include "bsp_clock.h"
#include "bsp_log.h"
#include "drv_tim.h"
#include "drv_usart.h"
#include "drv_gpio.h"
#include "zb_link.h"
#include "wifi_link.h"
#include "oled_view.h"
#include "thresholds.h"
#include "json.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>

static thresholds_t s_th;
static uint32_t s_last_pc_rx_ms = 0;
static uint32_t s_zb_join_window_start_ms = 0;
static uint32_t s_zb_join_retry_ms = 0;
static uint8_t s_zb_join_window_active = 0;
static uint8_t s_n1_oled_cache_valid = 0;
static uint32_t s_n1_oled_cache_update_ms = 0;
static int16_t s_n1_oled_temp_x100 = 0;
static uint16_t s_n1_oled_hum_x100 = 0;
static uint8_t s_n2_oled_cache_valid = 0;
static uint32_t s_n2_oled_cache_update_ms = 0;
static uint16_t s_n2_oled_lux = 0;

static void on_zb_frame(const frame_t *f) { router_on_zb_frame(f); }
static void on_wifi_line(const char *line, uint16_t n)
{
    s_last_pc_rx_ms = ms_now();
    router_on_json_line(line, n);
}

static void iwdg_init(void)
{
    /* 2s 超时：LSI≈40kHz，预分频 64，reload = 2000*40/64 ≈ 1250 */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1250);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

static void send_hello(void)
{
    char buf[96];
    int n = json_begin(buf, sizeof(buf));
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "t", "hello");
    n = json_add_str(buf, sizeof(buf), (uint16_t)n, "fw", "znjj-0.1");
    n = json_add_uint(buf, sizeof(buf), (uint16_t)n, "gw_id", 1);
    n = json_end(buf, sizeof(buf), (uint16_t)n);
    if (n > 0) (void)wifi_link_send_line(buf, (uint16_t)n);
}

/* 冷启动 / ZB 链路恢复 后自动开放入网窗 60s，让节点首次上电能加入。
 * Program.txt 未要求 PC 主动下发 allow_join，必须网关自己保证开机可用。 */
static void open_join_window(const char *reason)
{
    uint8_t sec = ZB_ALLOW_JOIN_SEC;

    LOGI("zb", "tx ALLOW_JOIN sec=%u src=%s", sec, reason);
    if (zb_link_send(CMD_ZB_ALLOW_JOIN, &sec, 1) != 0) {
        LOGW("zb", "tx ALLOW_JOIN failed src=%s", reason);
    }
}

static void request_node_snapshot(const char *reason)
{
    LOGI("zb", "tx LIST_NODES src=%s", reason);
    if (zb_link_send(CMD_ZB_LIST_NODES, 0, 0) != 0) {
        LOGW("zb", "tx LIST_NODES failed src=%s", reason);
    }
}

static void start_join_window(uint32_t now_ms, const char *reason)
{
    s_zb_join_window_active = 1u;
    s_zb_join_window_start_ms = now_ms;
    s_zb_join_retry_ms = now_ms;

    LOGI("zb", "bringup window start src=%s dur=%lu retry=%lu",
         reason, (unsigned long)ZB_BRINGUP_WINDOW_MS, (unsigned long)ZB_BRINGUP_RETRY_MS);
    open_join_window(reason);
    request_node_snapshot(reason);
}

void gw_main_init(void)
{
    bsp_clock_init();
    drv_tim_init();
    drv_gpio_init_board();
    drv_usart_init_all();
    bsp_log_init();
    LOGI("gw", "boot, f=%lu", (unsigned long)SystemCoreClock);

    if (config_load(&s_th) != 0) {
        config_default(&s_th);
        LOGW("gw", "cfg defaulted");
    }
    router_init();
    automation_init(&s_th);
    s_last_pc_rx_ms = 0;
    s_zb_join_window_start_ms = 0;
    s_zb_join_retry_ms = 0;
    s_zb_join_window_active = 0;
    s_n1_oled_cache_valid = 0;
    s_n1_oled_cache_update_ms = 0;
    s_n1_oled_temp_x100 = 0;
    s_n1_oled_hum_x100 = 0;
    s_n2_oled_cache_valid = 0;
    s_n2_oled_cache_update_ms = 0;
    s_n2_oled_lux = 0;

    zb_link_init(on_zb_frame);
    wifi_link_init(on_wifi_line);
    oled_view_init();

    /* 复位 CC2530 让其上电到已知态 */
    cc2530_reset_pulse();

    iwdg_init();
}

void gw_main_run(void)
{
    uint8_t  wifi_was_up = 0;
    uint8_t  zb_was_alive = 0;
    oled_model_t om;

    for (;;) {
        uint32_t now = ms_now();
        uint8_t wifi_up, zb_alive;
        uint8_t node_count;

        zb_link_poll(now);
        wifi_link_poll(now);
        zb_link_tick(now);
        wifi_link_tick(now);
        automation_tick(now);

        /* 沿触发：每次 WiFi 重连后重发 hello */
        wifi_up = wifi_link_is_up();
        if (wifi_up && !wifi_was_up) send_hello();
        wifi_was_up = wifi_up;

        /* 沿触发：每次 CC2530 (再)就绪后开一次入网窗 60s */
        zb_alive = zb_link_alive();
        if (zb_alive && !zb_was_alive) {
            start_join_window(now, "alive-edge");
        } else if (!zb_alive && zb_was_alive) {
            LOGW("zb", "coord link lost, stop bringup window");
            s_zb_join_window_active = 0u;
        }

        node_count = router_node_count();
        if (s_zb_join_window_active) {
            if (node_count != 0u) {
                LOGI("zb", "bringup window stop reason=node-detected count=%u", node_count);
                s_zb_join_window_active = 0u;
            } else if ((now - s_zb_join_window_start_ms) >= ZB_BRINGUP_WINDOW_MS) {
                LOGW("zb", "bringup window timeout");
                s_zb_join_window_active = 0u;
            } else if ((now - s_zb_join_retry_ms) >= ZB_BRINGUP_RETRY_MS) {
                s_zb_join_retry_ms = now;
                open_join_window("bringup-retry");
                request_node_snapshot("bringup-retry");
            }
        }
        zb_was_alive = zb_alive;

        /* 更新 OLED 模型 */
        {
            const node_state_t *n1 = router_node(router_node1_id());
            const node_state_t *n2 = router_node(router_node2_id());
            memset(&om, 0, sizeof(om));
            om.zb_ok = zb_link_alive();
            om.wifi_ok = wifi_link_is_up();
            /* P:1 条件：WiFi 透传已建链（= TCP 与服务器通路存活）。
             * 若仅以"PC 是否给我们发过数据"判断，只读不写的 PC 工具永远显示 P:0，
             * 而我们在透传态的 30 s 静默看门狗（§4.3）会在断连时自动拉下 wifi_link_is_up()。 */
            om.pc_ok = wifi_up ? 1u : 0u;
            if (s_last_pc_rx_ms != 0 && (now - s_last_pc_rx_ms) > TCP_DEAD_MS) {
                /* 长时间没有任何下行，提示为 0，哪怕透传还没掉 */
                om.pc_ok = 0u;
            }
            if (n1) {
                if (!n1->online) {
                    s_n1_oled_cache_valid = 0u;
                    s_n1_oled_cache_update_ms = 0u;
                } else if (n1->last_update_ms != 0u &&
                           n1->last_update_ms != s_n1_oled_cache_update_ms) {
                    s_n1_oled_cache_valid = 1u;
                    s_n1_oled_cache_update_ms = n1->last_update_ms;
                    s_n1_oled_temp_x100 = n1->temp_x100;
                    s_n1_oled_hum_x100 = n1->hum_x100;
                }
            }
            if (n2) {
                if (!n2->online) {
                    s_n2_oled_cache_valid = 0u;
                    s_n2_oled_cache_update_ms = 0u;
                } else if (n2->last_update_ms != 0u &&
                           n2->last_update_ms != s_n2_oled_cache_update_ms) {
                    s_n2_oled_cache_valid = 1u;
                    s_n2_oled_cache_update_ms = n2->last_update_ms;
                    s_n2_oled_lux = n2->lux;
                }
            }
            if (n1 && n1->online && s_n1_oled_cache_valid &&
                (now - n1->last_update_ms) <= NODE1_STALE_MS) {
                om.n1_online = 1u;
                om.n1_temp_x100 = s_n1_oled_temp_x100;
                om.n1_hum_x100 = s_n1_oled_hum_x100;
            }
            if (n2 && n2->online && s_n2_oled_cache_valid &&
                (now - n2->last_update_ms) <= NODE2_STALE_MS) {
                om.n2_online = 1u;
                om.n2_lux = s_n2_oled_lux;
            }
            strncpy(om.alm, automation_alarm_text(), sizeof(om.alm) - 1);
            oled_view_update(&om);
            oled_view_tick(now);
        }

        IWDG_ReloadCounter();
    }
}
