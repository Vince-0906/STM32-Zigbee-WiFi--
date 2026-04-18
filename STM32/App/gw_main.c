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

static void on_zb_frame(const frame_t *f) { router_on_zb_frame(f); }
static void on_wifi_line(const char *line, uint16_t n) { router_on_json_line(line, n); }

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
static void open_join_window(void)
{
    uint8_t sec = 60;
    (void)zb_link_send(CMD_ZB_ALLOW_JOIN, &sec, 1);
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
        if (zb_alive && !zb_was_alive) open_join_window();
        zb_was_alive = zb_alive;

        /* 更新 OLED 模型 */
        {
            const node_state_t *n1 = router_node(router_node1_id());
            const node_state_t *n2 = router_node(router_node2_id());
            memset(&om, 0, sizeof(om));
            om.zb_ok = zb_link_alive();
            om.wifi_ok = wifi_link_is_up();
            om.pc_ok = 0;
            if (n1) { om.n1_online = n1->online; om.n1_temp_x100 = n1->temp_x100; om.n1_hum_x100 = n1->hum_x100; }
            if (n2) { om.n2_online = n2->online; om.n2_lux = n2->lux; }
            strncpy(om.alm, automation_alarm_text(), sizeof(om.alm) - 1);
            oled_view_update(&om);
            oled_view_tick(now);
        }

        IWDG_ReloadCounter();
    }
}
