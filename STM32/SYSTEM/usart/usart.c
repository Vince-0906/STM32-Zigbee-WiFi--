#include "sys.h"
#include "usart.h"
#include "string.h"

#if 1
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
    (void)x;
}

int fputc(int ch, FILE *f)
{
    (void)f;
    USART1_SendByte((u8)ch);
    return ch;
}
#endif

volatile u8 UART1_rcv[UART1_RCV_BUFFER_SIZE];
volatile u8 UART1_rcv_count = 0U;
volatile u8 usart1_flag = 0U;
volatile char UART2_rcv[UART2_RCV_BUFFER_SIZE];
volatile u16 UART2_rcv_count = 0U;
static volatile u8 usart2_flag = 0U;

void USART1_SendByte(u8 byte)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }

    USART_SendData(USART1, byte);

    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    {
    }
}

void USART1_SendString(const char *str)
{
    if (str == 0)
    {
        return;
    }

    while (*str != '\0')
    {
        USART1_SendByte((u8)*str);
        str++;
    }
}

void USART2_SendByte(u8 byte)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
    {
    }

    USART_SendData(USART2, byte);

    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
    {
    }
}

void USART2_SendString(const char *str)
{
    if (str == 0)
    {
        return;
    }

    while (*str != '\0')
    {
        USART2_SendByte((u8)*str);
        str++;
    }
}

void uart_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
    USART_Cmd(USART1, ENABLE);
}

void uart2_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    UART2_ClearBuffer();
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

void UART2_ClearBuffer(void)
{
    __disable_irq();
    memset((void *)UART2_rcv, 0, sizeof(UART2_rcv));
    UART2_rcv_count = 0U;
    usart2_flag = 0U;
    __enable_irq();
}

u8 UART2_BufferContains(const char *pattern)
{
    char snapshot[UART2_RCV_BUFFER_SIZE];
    u16 count;

    if ((pattern == 0) || (*pattern == '\0'))
    {
        return 0U;
    }

    __disable_irq();
    count = UART2_rcv_count;
    if (count >= (UART2_RCV_BUFFER_SIZE - 1U))
    {
        count = UART2_RCV_BUFFER_SIZE - 1U;
    }
    memcpy(snapshot, (const void *)UART2_rcv, count);
    snapshot[count] = '\0';
    __enable_irq();

    return (strstr(snapshot, pattern) != 0) ? 1U : 0U;
}

void USART1_IRQHandler(void)
{
    u8 received_byte;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        received_byte = (u8)USART_ReceiveData(USART1);

        if (UART1_rcv_count >= UART1_RCV_BUFFER_SIZE)
        {
            UART1_rcv_count = 0U;
        }

        UART1_rcv[UART1_rcv_count++] = received_byte;
        usart1_flag = 1U;
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART2_IRQHandler(void)
{
    char received_byte;

    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        received_byte = (char)USART_ReceiveData(USART2);

        if (UART2_rcv_count >= (UART2_RCV_BUFFER_SIZE - 1U))
        {
            UART2_rcv_count = 0U;
        }

        UART2_rcv[UART2_rcv_count++] = received_byte;
        UART2_rcv[UART2_rcv_count] = '\0';
        usart2_flag = 1U;
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}
