/*
 * SSD1306 128x64 OLED 驱动（CC2530 协调器侧，P1.2=SCL, P1.3=SDA 软件 I2C）。
 * 初始化时序参考 references/OLED/HARDWARE/OLED/oled.c 中 OLED_Init()，
 * 该参考在 STM32F103 上上电即亮，本项目 STM32 侧 drv_oled.c 用同一时序也已实测 OK，
 * 因此这里拷贝同一组参数。
 */

#include "oled_ssd1306.h"

#include "soft_i2c.h"
#include "OnBoard.h"
#include "../../../../../../STM32/Hardware/OLED/oledfont.h"

#define OLED_ADDR      0x78u
#define OLED_CMD_MODE  0x00u
#define OLED_DAT_MODE  0x40u

/* MicroWait 是 NOP 循环（~265 ns/iter @32 MHz），不是真正的微秒校准；
 * 这里把它封装成粗略的毫秒延时，只在 OLED 上电稳定这类非时序关键场合使用。 */
static void oled_wait_ms(uint16 ms)
{
    while (ms-- != 0u) {
        MicroWait(4000);    /* 约 1 ms，够用 */
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

    /* SSD1306 上电后 VCC 需要稳定 ≥ 100 ms 才能可靠接收命令。 */
    oled_wait_ms(120);

    /* ---- 与 STM32 侧一致的 SSD1306 初始化序列 ---- */
    WR_CMD(0xAE);               /* display off */
    WR_CMD(0x00); WR_CMD(0x10); /* set column address */
    WR_CMD(0x40);               /* set start line */
    WR_CMD(0x81); WR_CMD(0xCF); /* contrast 0xCF */
    WR_CMD(0xA1);               /* seg remap */
    WR_CMD(0xC8);               /* COM scan dir */
    WR_CMD(0xA6);               /* normal display */
    WR_CMD(0xA8); WR_CMD(0x3F); /* multiplex 1/64 */
    WR_CMD(0xD3); WR_CMD(0x00); /* display offset 0 */
    WR_CMD(0xD5); WR_CMD(0x80); /* clock divide */
    WR_CMD(0xD9); WR_CMD(0xF1); /* pre-charge */
    WR_CMD(0xDA); WR_CMD(0x12); /* COM pins */
    WR_CMD(0xDB); WR_CMD(0x40); /* VCOMH */
    WR_CMD(0x20); WR_CMD(0x02); /* page addressing mode */
    WR_CMD(0x8D); WR_CMD(0x14); /* charge pump enable */
    WR_CMD(0xA4);               /* entire display on = RAM */
    WR_CMD(0xA6);               /* inverse off */
    WR_CMD(0xAF);               /* display on */

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

        /* 字形是 16 字节：前 8 字节画到 page，后 8 字节画到 page+1。 */
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
