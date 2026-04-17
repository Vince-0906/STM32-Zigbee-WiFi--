#ifndef __WIFI_H
#define __WIFI_H

#include "stm32f10x.h"

void WIFI_InitHardware(void);
u8 WIFI_Connect(void);
u8 WIFI_SendText(const char *text);
u8 WIFI_IsReady(void);

#endif
