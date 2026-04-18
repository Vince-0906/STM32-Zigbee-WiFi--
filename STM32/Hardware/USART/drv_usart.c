/*
 * USART1/2/UART4 驱动：RX 中断环形 + TX 阻塞轮询。
 * 硬约束（§10 #4）：ISR 只搬字节；不在 ISR 调用任何解析/业务。
 */

#include "drv_usart.h"
#include "stm32f10x.h"
#include <string.h>

#define RX_RING_SZ   256u    /* 2 的幂次 */

typedef struct {
    USART_TypeDef *u;
    volatile uint8_t  ring[RX_RING_SZ];
    volatile uint16_t head;
    volatile uint16_t tail;
} uring_t;

static uring_t s_log = { USART1, {0}, 0, 0 };
static uring_t s_wifi = { USART2, {0}, 0, 0 };
static uring_t s_zb   = { UART4,  {0}, 0, 0 };

static void ring_push(uring_t *r, uint8_t b)
{
    uint16_t nx = (uint16_t)((r->head + 1) & (RX_RING_SZ - 1));
    if (nx == r->tail) return;      /* 溢出，丢字节 */
    r->ring[r->head] = b;
    r->head = nx;
}

static uint16_t ring_pop(uring_t *r, uint8_t *out, uint16_t cap)
{
    uint16_t got = 0;
    while (got < cap && r->tail != r->head) {
        out[got++] = r->ring[r->tail];
        r->tail = (uint16_t)((r->tail + 1) & (RX_RING_SZ - 1));
    }
    return got;
}

static uint16_t ring_avail(uring_t *r)
{
    return (uint16_t)((r->head - r->tail) & (RX_RING_SZ - 1));
}

static int uart_write_blocking(USART_TypeDef *u, const void *data, uint16_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t i;
    for (i = 0; i < n; ++i) {
        while (USART_GetFlagStatus(u, USART_FLAG_TXE) == RESET) { /* busy wait */ }
        USART_SendData(u, p[i]);
    }
    while (USART_GetFlagStatus(u, USART_FLAG_TC) == RESET) { /* flush */ }
    return n;
}

/* ---------- 初始化 ---------- */
static void usart_common_init(USART_TypeDef *u)
{
    USART_InitTypeDef ui;
    ui.USART_BaudRate = 115200;
    ui.USART_WordLength = USART_WordLength_8b;
    ui.USART_StopBits = USART_StopBits_1;
    ui.USART_Parity = USART_Parity_No;
    ui.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    ui.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(u, &ui);
    USART_ITConfig(u, USART_IT_RXNE, ENABLE);
    USART_Cmd(u, ENABLE);
}

void drv_usart_init_all(void)
{
    GPIO_InitTypeDef gi;
    NVIC_InitTypeDef ni;

    /* USART1 PA9 TX / PA10 RX */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    gi.GPIO_Pin = GPIO_Pin_9;  gi.GPIO_Mode = GPIO_Mode_AF_PP;     gi.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = GPIO_Pin_10; gi.GPIO_Mode = GPIO_Mode_IN_FLOATING;                                 GPIO_Init(GPIOA, &gi);
    usart_common_init(USART1);
    ni.NVIC_IRQChannel = USART1_IRQn; ni.NVIC_IRQChannelPreemptionPriority = 1;
    ni.NVIC_IRQChannelSubPriority = 0; ni.NVIC_IRQChannelCmd = ENABLE; NVIC_Init(&ni);

    /* USART2 PA2 TX / PA3 RX */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    gi.GPIO_Pin = GPIO_Pin_2; gi.GPIO_Mode = GPIO_Mode_AF_PP;  GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = GPIO_Pin_3; gi.GPIO_Mode = GPIO_Mode_IN_FLOATING; GPIO_Init(GPIOA, &gi);
    usart_common_init(USART2);
    ni.NVIC_IRQChannel = USART2_IRQn; NVIC_Init(&ni);

    /* UART4 PC10 TX / PC11 RX */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gi.GPIO_Pin = GPIO_Pin_10; gi.GPIO_Mode = GPIO_Mode_AF_PP;  GPIO_Init(GPIOC, &gi);
    gi.GPIO_Pin = GPIO_Pin_11; gi.GPIO_Mode = GPIO_Mode_IN_FLOATING; GPIO_Init(GPIOC, &gi);
    usart_common_init(UART4);
    ni.NVIC_IRQChannel = UART4_IRQn; NVIC_Init(&ni);
}

/* ---------- IRQ ---------- */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        ring_push(&s_log, (uint8_t)USART_ReceiveData(USART1));
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        ring_push(&s_wifi, (uint8_t)USART_ReceiveData(USART2));
    }
}

void UART4_IRQHandler(void)
{
    if (USART_GetITStatus(UART4, USART_IT_RXNE) != RESET) {
        ring_push(&s_zb, (uint8_t)USART_ReceiveData(UART4));
    }
}

/* ---------- API ---------- */
int usart_log_write(const void *data, uint16_t n) { return uart_write_blocking(USART1, data, n); }
int usart_log_putc(char c) { return uart_write_blocking(USART1, &c, 1); }
int usart_log_puts(const char *s) { return uart_write_blocking(USART1, s, (uint16_t)strlen(s)); }

int wifi_uart_write(const void *data, uint16_t n) { return uart_write_blocking(USART2, data, n); }
int wifi_uart_read(uint8_t *out, uint16_t cap) { return (int)ring_pop(&s_wifi, out, cap); }
uint16_t wifi_uart_rx_available(void) { return ring_avail(&s_wifi); }

int zb_uart_write(const void *data, uint16_t n) { return uart_write_blocking(UART4, data, n); }
int zb_uart_read(uint8_t *out, uint16_t cap) { return (int)ring_pop(&s_zb, out, cap); }
uint16_t zb_uart_rx_available(void) { return ring_avail(&s_zb); }
