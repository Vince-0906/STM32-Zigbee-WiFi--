/*
 * 节点侧 OLED 视图：将 node_oled_model_t 渲染到 4 行（每行 16 个 8x16 字符）。
 * 不依赖 stdio/printf（IAR-8051 上 sprintf 体积太大且容易栈溢出），手写小整数转字符。
 */

#include "node_oled_view.h"
#include "oled_ssd1306.h"
#include "board_e18ms1.h"

static node_oled_model_t s_model;
static node_oled_model_t s_last;
static uint8             s_initialized = 0;

static void line_clear(char *buf)
{
    uint8 i;
    for (i = 0; i < 16u; ++i) buf[i] = ' ';
    buf[16] = '\0';
}

static void put_char(char *buf, uint8 *idx, char c)
{
    if (*idx < 16u) {
        buf[*idx] = c;
        (*idx)++;
    }
}

static void put_str(char *buf, uint8 *idx, const char *s)
{
    while (*s != '\0' && *idx < 16u) {
        buf[*idx] = *s;
        (*idx)++;
        s++;
    }
}

static void put_u16(char *buf, uint8 *idx, uint16 value)
{
    char tmp[6];
    uint8 len = 0;

    do {
        tmp[len++] = (char)('0' + (value % 10u));
        value = (uint16)(value / 10u);
    } while (value != 0u && len < sizeof(tmp));

    while (len > 0u) {
        put_char(buf, idx, tmp[--len]);
    }
}

static void put_hex16(char *buf, uint8 *idx, uint16 value)
{
    uint8 shift;
    for (shift = 0u; shift < 4u; ++shift) {
        uint8 nibble = (uint8)((value >> ((3u - shift) * 4u)) & 0x0Fu);
        put_char(buf, idx, (char)(nibble < 10u ? ('0' + nibble) : ('A' + nibble - 10u)));
    }
}

static void put_temp_x100(char *buf, uint8 *idx, int16 value)
{
    uint16 abs_v;

    if (value < 0) {
        put_char(buf, idx, '-');
        abs_v = (uint16)(-value);
    } else {
        abs_v = (uint16)value;
    }
    put_u16(buf, idx, (uint16)(abs_v / 100u));
    put_char(buf, idx, '.');
    put_char(buf, idx, (char)('0' + ((abs_v / 10u) % 10u)));
    put_char(buf, idx, (char)('0' + (abs_v % 10u)));
}

static void render(void)
{
    char line[17];
    uint8 idx;

    /* Row 0：节点角色 + 网络状态 */
    line_clear(line);
    idx = 0;
    if (s_model.role == 2u) {
        put_str(line, &idx, "N2 Lux ");
    } else {
        put_str(line, &idx, "N1 TempHum ");
    }
    put_str(line, &idx, s_model.online ? "OK" : "..");
    oled_ssd1306_draw_text(0, line);

    /* Row 1：短地址 + 信道 */
    line_clear(line);
    idx = 0;
    put_str(line, &idx, "ID:");
    put_hex16(line, &idx, s_model.short_addr);
    put_str(line, &idx, " CH:");
    put_u16(line, &idx, s_model.channel);
    oled_ssd1306_draw_text(1, line);

    /* Row 2：传感数据 */
    line_clear(line);
    idx = 0;
    if (!s_model.has_sample) {
        put_str(line, &idx, "----");
    } else if (s_model.role == 2u) {
        put_str(line, &idx, "L:");
        put_u16(line, &idx, s_model.lux);
        put_str(line, &idx, " lx");
    } else {
        put_str(line, &idx, "T:");
        put_temp_x100(line, &idx, s_model.temp_x100);
        put_str(line, &idx, " H:");
        put_u16(line, &idx, (uint16)(s_model.hum_x100 / 100u));
    }
    oled_ssd1306_draw_text(2, line);

    /* Row 3：执行器状态 */
    line_clear(line);
    idx = 0;
    put_str(line, &idx, "LED:");
    put_str(line, &idx, s_model.led_on ? "ON " : "OFF");
    put_str(line, &idx, " BZ:");
    put_str(line, &idx, s_model.buzzer_on ? "ON" : "OFF");
    oled_ssd1306_draw_text(3, line);
}

static uint8 model_changed(void)
{
    /* 简单 memcmp 替代：逐字段判等，避免拉进 string.h 依赖 */
    if (s_model.role        != s_last.role)        return 1u;
    if (s_model.online      != s_last.online)      return 1u;
    if (s_model.short_addr  != s_last.short_addr)  return 1u;
    if (s_model.channel     != s_last.channel)     return 1u;
    if (s_model.temp_x100   != s_last.temp_x100)   return 1u;
    if (s_model.hum_x100    != s_last.hum_x100)    return 1u;
    if (s_model.lux         != s_last.lux)         return 1u;
    if (s_model.led_on      != s_last.led_on)      return 1u;
    if (s_model.buzzer_on   != s_last.buzzer_on)   return 1u;
    if (s_model.has_sample  != s_last.has_sample)  return 1u;
    return 0u;
}

void node_oled_view_init(uint8 role)
{
    uint8 i;
    uint8 *p;

    /* memset 替代：byte-wise 清零，避免依赖 string.h */
    p = (uint8 *)&s_model;
    for (i = 0; i < sizeof(s_model); ++i) p[i] = 0;
    p = (uint8 *)&s_last;
    for (i = 0; i < sizeof(s_last); ++i)  p[i] = 0xFFu;   /* 强制首次 render */

    s_model.role = role;
    oled_ssd1306_init();
    s_initialized = 1u;
    render();
    s_last = s_model;
}

void node_oled_view_set(const node_oled_model_t *m)
{
    if (m != 0) {
        /* 不直接 = *m 以保留 role（init 已固定） */
        uint8 saved_role = s_model.role;
        s_model = *m;
        s_model.role = saved_role;
    }
}

void node_oled_view_refresh(void)
{
    if (!s_initialized) return;
    if (!model_changed()) return;
    render();
    s_last = s_model;
}
