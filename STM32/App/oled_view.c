/*
 * OLED 视图服务：
 *   - 纯排版层，底层像素驱动见 drv/drv_oled.{c,h}。
 *   - 模型缓存 + 节流刷屏（OLED_REFRESH_MS 或模型变化触发，§5.5）。
 *   - 8x16 字体，128x64 屏 = 16 字符 x 4 行；超出宽度部分自动截断。
 */

#include "oled_view.h"
#include "config.h"
#include "drv_oled.h"
#include <stdio.h>
#include <string.h>

static oled_model_t s_model;
static oled_model_t s_last;
static uint32_t     s_last_draw_ms = 0;

void oled_view_init(void)
{
    drv_oled_init();
    memset(&s_model, 0, sizeof(s_model));
    memset(&s_last, 0xFF, sizeof(s_last));
}

void oled_view_update(const oled_model_t *m)
{
    if (m) s_model = *m;
}

static int model_changed(void)
{
    return memcmp(&s_model, &s_last, sizeof(oled_model_t)) != 0;
}

void oled_view_tick(uint32_t now_ms)
{
    char line[32];
    int16_t t100;

    if (!model_changed() && (now_ms - s_last_draw_ms) < OLED_REFRESH_MS) return;
    s_last_draw_ms = now_ms;
    s_last = s_model;

    drv_oled_clear();

    /* 行 0 (y=0)：状态栏 */
    snprintf(line, sizeof(line), "GW ZB:%c WF:%c P:%c",
             s_model.zb_ok ? 'O' : 'X',
             s_model.wifi_ok ? 'O' : 'X',
             s_model.pc_ok ? '1' : '0');
    drv_oled_show_string(0, 0, line);

    /* 行 1 (y=16)：Node1 温湿 */
    if (s_model.n1_online) {
        t100 = s_model.n1_temp_x100;
        snprintf(line, sizeof(line), "N1 T:%d.%02d H:%u",
                 (int)(t100 / 100),
                 (int)((t100 < 0 ? -t100 : t100) % 100),
                 (unsigned)(s_model.n1_hum_x100 / 100));
    } else {
        snprintf(line, sizeof(line), "N1 -- --");
    }
    drv_oled_show_string(0, 16, line);

    /* 行 2 (y=32)：Node2 光 */
    if (s_model.n2_online) {
        snprintf(line, sizeof(line), "N2 L:%u lx", (unsigned)s_model.n2_lux);
    } else {
        snprintf(line, sizeof(line), "N2 --");
    }
    drv_oled_show_string(0, 32, line);

    /* 行 3 (y=48)：ALM */
    snprintf(line, sizeof(line), "ALM:%s", s_model.alm[0] ? s_model.alm : "---");
    drv_oled_show_string(0, 48, line);

    drv_oled_refresh();
}
