#ifndef __DRV_USART_H__
#define __DRV_USART_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 三路串口：USART1 日志、USART2 WiFi、UART4 Zigbee。
 * 每路带 256B RX 环形 + 64B TX 轮询。ISR 只搬字节。
 */

/* 公共：写/读接口 */
int  usart_log_write(const void *data, uint16_t n);
int  usart_log_putc(char c);
int  usart_log_puts(const char *s);

int  wifi_uart_write(const void *data, uint16_t n);
int  wifi_uart_read(uint8_t *out, uint16_t cap);    /* 非阻塞；返回实际读到的字节 */
uint16_t wifi_uart_rx_available(void);

int  zb_uart_write(const void *data, uint16_t n);
int  zb_uart_read(uint8_t *out, uint16_t cap);
uint16_t zb_uart_rx_available(void);

/* 初始化 */
void drv_usart_init_all(void);

#ifdef __cplusplus
}
#endif

#endif
