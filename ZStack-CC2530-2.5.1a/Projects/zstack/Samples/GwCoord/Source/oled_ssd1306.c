#include "oled_ssd1306.h"

#include "soft_i2c.h"
#include "../../../../../../STM32/Hardware/OLED/oledfont.h"

#define OLED_ADDR      0x78u
#define OLED_CMD_MODE  0x00u
#define OLED_DAT_MODE  0x40u

static void oled_write(uint8 mode, uint8 value)
{
    soft_i2c_start();
    (void)soft_i2c_write_byte(OLED_ADDR);
    (void)soft_i2c_write_byte(mode);
    (void)soft_i2c_write_byte(value);
    soft_i2c_stop();
}

static void oled_set_pos(uint8 x, uint8 page)
{
    oled_write(OLED_CMD_MODE, (uint8)(0xB0u | page));
    oled_write(OLED_CMD_MODE, (uint8)(0x10u | ((x >> 4) & 0x0Fu)));
    oled_write(OLED_CMD_MODE, (uint8)(x & 0x0Fu));
}

static void oled_fill_page(uint8 page, uint8 value)
{
    uint8 x;

    oled_set_pos(0, page);
    for (x = 0; x < 128u; ++x) {
        oled_write(OLED_DAT_MODE, value);
    }
}

void oled_ssd1306_init(void)
{
    static const uint8 init_seq[] = {
        0xAE, 0x20, 0x10, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    uint8 i;

    soft_i2c_init();
    for (i = 0; i < sizeof(init_seq); ++i) {
        oled_write(OLED_CMD_MODE, init_seq[i]);
    }
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
        oled_set_pos(x, page);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][0]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][1]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][2]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][3]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][4]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][5]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][6]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][7]);

        oled_set_pos(x, (uint8)(page + 1u));
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][8]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][9]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][10]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][11]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][12]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][13]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][14]);
        oled_write(OLED_DAT_MODE, asc2_1608[ch - 0x20u][15]);

        x = (uint8)(x + 8u);
    }
}
