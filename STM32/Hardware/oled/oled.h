#ifndef __OLED_H
#define __OLED_H

#include "sys.h"

#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_ADDRESS        0x78

#define OLED_SDA_PIN        GPIO_Pin_11
#define OLED_SCL_PIN        GPIO_Pin_12

#define OLED_SDA            PAout(11)
#define OLED_SCL            PAout(12)

#define OLED_CMD            0
#define OLED_DATA           1

void OLED_Init(void);
void OLED_Refresh(void);
void OLED_Clear(void);
void OLED_DrawPoint(u8 x, u8 y);
void OLED_ShowChar(u8 x, u8 y, char chr);
void OLED_ShowString(u8 x, u8 y, const char *str);
void OLED_ShowNum(u8 x, u8 y, u32 num, u8 len);
void OLED_DrawProgressBar(u8 x, u8 y, u8 width, u8 height, u8 percent);

#endif
