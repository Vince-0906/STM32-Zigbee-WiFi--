/*
 * SSD1306 128x64 OLED 驱动（软件 I2C：PA11=SDA, PA12=SCL）。
 * 写法沿用 D:\STM32\references\OLED\HARDWARE\OLED\oled.c 的 GRAM + Refresh 套路，
 * 但 GRAM 尺寸裁到 128 列 x 8 页（屏幕实际尺寸）。
 *
 * 字符绘制用 8x16 字模（oledfont.h 的 asc2_1608）：每字符 16 字节 = 8 列 x 2 字节
 * （列上 8 行 + 列下 8 行），bit7 = 最顶像素。
 */

#include "drv_oled.h"
#include "drv_tim.h"
#include "oledfont.h"
#include "stm32f10x.h"

#define OLED_SDA_PIN   GPIO_Pin_11
#define OLED_SCL_PIN   GPIO_Pin_12
#define OLED_PORT      GPIOA

#define OLED_ADDR      0x78   /* 7 位地址 0x3C 左移 1 */
#define OLED_CMD_MODE  0x00
#define OLED_DAT_MODE  0x40

static uint8_t s_gram[8][128];   /* 8 页 × 128 列 */

/* ---------- 软件 I2C ---------- */
static void scl_set(uint8_t v) { if (v) GPIO_SetBits(OLED_PORT, OLED_SCL_PIN); else GPIO_ResetBits(OLED_PORT, OLED_SCL_PIN); }
static void sda_set(uint8_t v) { if (v) GPIO_SetBits(OLED_PORT, OLED_SDA_PIN); else GPIO_ResetBits(OLED_PORT, OLED_SDA_PIN); }
static void i2c_delay(void)    { volatile int i; for (i = 0; i < 4; ++i) __NOP(); }

static void i2c_start(void)
{
    sda_set(1); scl_set(1); i2c_delay();
    sda_set(0); i2c_delay();
    scl_set(0);
}

static void i2c_stop(void)
{
    scl_set(1); sda_set(0); i2c_delay();
    sda_set(1); i2c_delay();
}

static void i2c_wait_ack(void)
{
    /* 不采样 SDA：软件 I2C 模拟一拍 SCL 即可 */
    scl_set(1); i2c_delay();
    scl_set(0);
}

static void i2c_send_byte(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8; ++i) {
        scl_set(0);
        sda_set((dat & 0x80) ? 1 : 0);
        scl_set(1);
        scl_set(0);
        dat <<= 1;
    }
}

static void oled_write(uint8_t dat, uint8_t mode)
{
    i2c_start();
    i2c_send_byte(OLED_ADDR);
    i2c_wait_ack();
    i2c_send_byte(mode ? OLED_DAT_MODE : OLED_CMD_MODE);
    i2c_wait_ack();
    i2c_send_byte(dat);
    i2c_wait_ack();
    i2c_stop();
}

#define WR_CMD(c)  oled_write((c), 0)
#define WR_DAT(d)  oled_write((d), 1)

/* ---------- 接口 ---------- */
void drv_oled_clear(void)
{
    uint8_t p, c;
    for (p = 0; p < 8; ++p)
        for (c = 0; c < 128; ++c) s_gram[p][c] = 0x00;
}

void drv_oled_refresh(void)
{
    uint8_t p, c;
    for (p = 0; p < 8; ++p) {
        WR_CMD((uint8_t)(0xB0 + p));   /* 页地址 */
        WR_CMD(0x00);                  /* 列低 4 位 */
        WR_CMD(0x10);                  /* 列高 4 位 */
        for (c = 0; c < 128; ++c) WR_DAT(s_gram[p][c]);
    }
}

void drv_oled_draw_point(uint8_t x, uint8_t y, uint8_t on)
{
    uint8_t page, bit;
    if (x >= 128 || y >= 64) return;
    page = (uint8_t)(y >> 3);
    bit  = (uint8_t)(1u << (y & 7));
    if (on) s_gram[page][x] |= bit;
    else    s_gram[page][x] &= (uint8_t)~bit;
}

void drv_oled_show_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t col, row, temp;
    uint8_t x0 = x, y0 = y;
    const unsigned char *g;

    if (ch < ' ' || ch > '~') ch = ' ';
    g = asc2_1608[(uint8_t)ch - ' '];

    /* 字模为 8 列 x 2 字节：先顶 8 行再底 8 行，bit7 = 最顶像素。 */
    for (col = 0; col < 16; ++col) {
        temp = g[col];
        for (row = 0; row < 8; ++row) {
            drv_oled_draw_point(x, y, (temp & 0x80) ? 1 : 0);
            temp <<= 1;
            y++;
            if ((y - y0) == 16) {
                y = y0;
                x++;
                break;
            }
        }
    }
    (void)x0;
}

void drv_oled_show_string(uint8_t x, uint8_t y, const char *s)
{
    while (*s && x + 8 <= 128) {
        drv_oled_show_char(x, y, *s);
        x = (uint8_t)(x + 8);
        s++;
    }
}

/* ---------- 初始化 ---------- */
static void oled_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gi.GPIO_Pin = OLED_SDA_PIN | OLED_SCL_PIN;
    gi.GPIO_Mode = GPIO_Mode_Out_PP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_PORT, &gi);
    scl_set(1);
    sda_set(1);
}

void drv_oled_init(void)
{
    oled_gpio_init();
    drv_delay_ms(50);

    WR_CMD(0xAE);              /* display off */
    WR_CMD(0x00); WR_CMD(0x10);/* set column address */
    WR_CMD(0x40);              /* set start line */
    WR_CMD(0x81); WR_CMD(0xCF);/* contrast */
    WR_CMD(0xA1);              /* seg remap */
    WR_CMD(0xC8);              /* com scan dir */
    WR_CMD(0xA6);              /* normal display */
    WR_CMD(0xA8); WR_CMD(0x3F);/* multiplex 1/64 */
    WR_CMD(0xD3); WR_CMD(0x00);/* display offset */
    WR_CMD(0xD5); WR_CMD(0x80);/* clock divide */
    WR_CMD(0xD9); WR_CMD(0xF1);/* pre-charge */
    WR_CMD(0xDA); WR_CMD(0x12);/* com pins */
    WR_CMD(0xDB); WR_CMD(0x40);/* vcomh */
    WR_CMD(0x20); WR_CMD(0x02);/* page addressing */
    WR_CMD(0x8D); WR_CMD(0x14);/* charge pump */
    WR_CMD(0xA4);              /* entire display on = RAM content */
    WR_CMD(0xA6);              /* inverse off */
    WR_CMD(0xAF);              /* display on */

    drv_oled_clear();
    drv_oled_refresh();
}
