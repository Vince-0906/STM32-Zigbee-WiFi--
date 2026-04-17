#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

#define UART1_RCV_BUFFER_SIZE   64U
#define UART2_RCV_BUFFER_SIZE   256U

extern volatile u8 UART1_rcv[UART1_RCV_BUFFER_SIZE];
extern volatile u8 UART1_rcv_count;
extern volatile u8 usart1_flag;
extern volatile char UART2_rcv[UART2_RCV_BUFFER_SIZE];
extern volatile u16 UART2_rcv_count;

void uart_init(u32 bound);
void uart2_init(u32 bound);
void USART1_SendByte(u8 byte);
void USART1_SendString(const char *str);
void USART2_SendByte(u8 byte);
void USART2_SendString(const char *str);
void UART2_ClearBuffer(void);
u8 UART2_BufferContains(const char *pattern);

#endif
