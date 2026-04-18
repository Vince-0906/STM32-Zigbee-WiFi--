#ifndef __ZBNODE_OLED_SSD1306_H__
#define __ZBNODE_OLED_SSD1306_H__

#include "ZComDef.h"

void oled_ssd1306_init(void);
void oled_ssd1306_clear(void);
void oled_ssd1306_draw_text(uint8 row, const char *text);

#endif
