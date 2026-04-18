#include "gw_oled_view.h"

#include "gw_types.h"
#include "zb_net.h"
#include "oled_ssd1306.h"

static void line_reset(char *buf)
{
    uint8 i;

    for (i = 0; i < 16u; ++i) {
        buf[i] = ' ';
    }
    buf[16] = '\0';
}

static void line_putc(char *buf, uint8 *idx, char c)
{
    if (*idx < 16u) {
        buf[*idx] = c;
        (*idx)++;
    }
}

static void line_puts(char *buf, uint8 *idx, const char *s)
{
    while (*s != '\0' && *idx < 16u) {
        buf[*idx] = *s;
        (*idx)++;
        s++;
    }
}

static void line_put_u16(char *buf, uint8 *idx, uint16 value)
{
    char tmp[6];
    uint8 len = 0;

    do {
        tmp[len++] = (char)('0' + (value % 10u));
        value = (uint16)(value / 10u);
    } while (value != 0u && len < sizeof(tmp));

    while (len > 0u) {
        line_putc(buf, idx, tmp[--len]);
    }
}

static void line_put_i16_x100(char *buf, uint8 *idx, int16 value)
{
    uint16 abs_v;

    if (value < 0) {
        line_putc(buf, idx, '-');
        abs_v = (uint16)(-value);
    } else {
        abs_v = (uint16)value;
    }

    line_put_u16(buf, idx, (uint16)(abs_v / 100u));
    line_putc(buf, idx, '.');
    line_putc(buf, idx, (char)('0' + ((abs_v / 10u) % 10u)));
    line_putc(buf, idx, (char)('0' + (abs_v % 10u)));
}

static void line_put_hex16(char *buf, uint8 *idx, uint16 value)
{
    uint8 shift;

    for (shift = 0u; shift < 4u; ++shift) {
        uint8 nibble = (uint8)((value >> ((3u - shift) * 4u)) & 0x0Fu);
        line_putc(buf, idx, (char)(nibble < 10u ? ('0' + nibble) : ('A' + nibble - 10u)));
    }
}

void gw_oled_view_init(void)
{
    oled_ssd1306_init();
}

void gw_oled_view_refresh(void)
{
    char line[17];
    uint8 idx;
    uint8 state;
    uint8 channel;
    uint16 panid;
    uint8 joined_n;
    const zb_node_t *n1;
    const zb_node_t *n2;

    zb_net_get_status(&state, &channel, &panid, &joined_n);
    n1 = zb_net_find_by_role(ZB_ROLE_TEMP_HUM);
    n2 = zb_net_find_by_role(ZB_ROLE_LUX);

    oled_ssd1306_clear();

    line_reset(line);
    idx = 0;
    line_puts(line, &idx, "ZB");
    line_put_u16(line, &idx, state);
    line_puts(line, &idx, " C");
    line_put_u16(line, &idx, channel);
    line_puts(line, &idx, " P");
    line_put_hex16(line, &idx, panid);
    oled_ssd1306_draw_text(0, line);

    line_reset(line);
    idx = 0;
    line_puts(line, &idx, "N1 ");
    if (n1 != 0 && (n1->flags & 0x01u)) {
        line_puts(line, &idx, "T:");
        line_put_i16_x100(line, &idx, n1->temp_x100);
        line_puts(line, &idx, " H:");
        line_put_u16(line, &idx, (uint16)(n1->hum_x100 / 100u));
    } else {
        line_puts(line, &idx, "-- --");
    }
    oled_ssd1306_draw_text(1, line);

    line_reset(line);
    idx = 0;
    line_puts(line, &idx, "N2 ");
    if (n2 != 0 && (n2->flags & 0x01u)) {
        line_puts(line, &idx, "L:");
        line_put_u16(line, &idx, n2->lux);
    } else {
        line_puts(line, &idx, "--");
    }
    oled_ssd1306_draw_text(2, line);

    line_reset(line);
    idx = 0;
    line_puts(line, &idx, "ALM:");
    if ((n1 != 0 && (n1->flags & 0x01u) == 0u) ||
        (n2 != 0 && (n2->flags & 0x01u) == 0u)) {
        line_puts(line, &idx, "OFFLINE");
    } else {
        line_puts(line, &idx, "---");
    }
    oled_ssd1306_draw_text(3, line);
    (void)joined_n;
}
