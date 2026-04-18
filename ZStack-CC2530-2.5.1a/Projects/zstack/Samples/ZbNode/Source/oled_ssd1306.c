/*
 * SSD1306 128x64 OLED 驱动（节点侧）。
 * 与 GwCoord/Source/oled_ssd1306.c 同源；初始化序列与 STM32 实测可亮的版本一致。
 */

#include "oled_ssd1306.h"

#include "soft_i2c.h"
#include "OnBoard.h"
#include "../../../../../../STM32/Hardware/OLED/oledfont.h"

#define OLED_ADDR      0x78u
#define OLED_CMD_MODE  0x00u
#define OLED_DAT_MODE  0x40u

static void oled_wait_ms(uint16 ms)
{
    while (ms-- != 0u) {
        MicroWait(4000);
    }
}

static void oled_write(uint8 mode, uint8 value)
{
    soft_i2c_start();
    (void)soft_i2c_write_byte(OLED_ADDR);
    (void)soft_i2c_write_byte(mode);
    (void)soft_i2c_write_byte(value);
    soft_i2c_stop();
}

#define WR_CMD(c)  oled_write(OLED_CMD_MODE, (uint8)(c))
#define WR_DAT(d)  oled_write(OLED_DAT_MODE, (uint8)(d))

static void oled_set_pos(uint8 x, uint8 page)
{
    WR_CMD(0xB0u | page);
    WR_CMD(0x10u | ((x >> 4) & 0x0Fu));
    WR_CMD(0x00u | (x & 0x0Fu));
}

static void oled_fill_page(uint8 page, uint8 value)
{
    uint8 x;

    oled_set_pos(0, page);
    for (x = 0; x < 128u; ++x) {
        WR_DAT(value);
    }
}

void oled_ssd1306_init(void)
{
    soft_i2c_init();
    oled_wait_ms(120);

    WR_CMD(0xAE);
    WR_CMD(0x00); WR_CMD(0x10);
    WR_CMD(0x40);
    WR_CMD(0x81); WR_CMD(0xCF);
    WR_CMD(0xA1);
    WR_CMD(0xC8);
    WR_CMD(0xA6);
    WR_CMD(0xA8); WR_CMD(0x3F);
    WR_CMD(0xD3); WR_CMD(0x00);
    WR_CMD(0xD5); WR_CMD(0x80);
    WR_CMD(0xD9); WR_CMD(0xF1);
    WR_CMD(0xDA); WR_CMD(0x12);
    WR_CMD(0xDB); WR_CMD(0x40);
    WR_CMD(0x20); WR_CMD(0x02);
    WR_CMD(0x8D); WR_CMD(0x14);
    WR_CMD(0xA4);
    WR_CMD(0xA6);
    WR_CMD(0xAF);

    oled_wait_ms(20);
    oled_ssd1306_clear();
}

void oled_ssd1306_clear(void)
{
    uint8 page;

    for (page = 0; page < 8u; ++page) {
        oled_fill_page(page, 0x00u);
    }
}

void oled_ssd1306_draw_text(uint8 row, const char *text)
{
    uint8 i;
    uint8 ch;
    uint8 x;
    uint8 page;
    uint8 col;
    const uint8 *glyph;

    if (row > 3u) {
        return;
    }

    page = (uint8)(row * 2u);
    x = 0u;
    for (i = 0; i < 16u; ++i) {
        ch = (text != 0 && text[i] != '\0') ? (uint8)text[i] : (uint8)' ';
        if (ch < 0x20u || ch > 0x7Eu) {
            ch = (uint8)'?';
        }
        glyph = (const uint8 *)&asc2_1608[ch - 0x20u][0];

        oled_set_pos(x, page);
        for (col = 0; col < 8u; ++col) {
            WR_DAT(glyph[col]);
        }
        oled_set_pos(x, (uint8)(page + 1u));
        for (col = 0; col < 8u; ++col) {
            WR_DAT(glyph[col + 8u]);
        }

        x = (uint8)(x + 8u);
    }
}
