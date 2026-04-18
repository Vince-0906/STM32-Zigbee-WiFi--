#ifndef __DRV_OLED_H__
#define __DRV_OLED_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SSD1306 128x64 驱动（软件 I2C，PA11=SDA, PA12=SCL）。
 * 所有绘制先写入 GRAM，最后调用 drv_oled_refresh() 刷到屏幕。
 * 字体固定 8x16（只支持 ASCII 0x20..0x7E）。
 */

void drv_oled_init(void);

void drv_oled_clear(void);                                      /* 清 GRAM（不刷屏）*/
void drv_oled_refresh(void);                                    /* GRAM -> 屏 */

void drv_oled_draw_point(uint8_t x, uint8_t y, uint8_t on);     /* x:0..127  y:0..63 */
void drv_oled_show_char(uint8_t x, uint8_t y, char ch);         /* 8x16，越界截断 */
void drv_oled_show_string(uint8_t x, uint8_t y, const char *s); /* 不自动换行 */

#ifdef __cplusplus
}
#endif

#endif
