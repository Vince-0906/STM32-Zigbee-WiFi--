#include "wifi.h"
#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "string.h"
#include <stdio.h>

#define WIFI_EN_GPIO_CLK             RCC_APB2Periph_GPIOB
#define WIFI_EN_GPIO_PORT            GPIOB
#define WIFI_EN_GPIO_PIN             GPIO_Pin_12
#define WIFI_EN                      PBout(12)

#define WIFI_MODE_CMD                "AT+WMODE=1,1\r\n"
#define WIFI_JOIN_CMD                "AT+WJAP=\"611-2\",\"hdsx61#203\"\r\n"
#define WIFI_SOCKET_CMD              "AT+SOCKET=4,192.168.62.68,23333\r\n"
#define WIFI_TRANSPARENT_CMD         "AT+SOCKETTT\r\n"

#define WIFI_CMD_RETRY_COUNT         3U
#define WIFI_POWER_CYCLE_DELAY_MS    100U
#define WIFI_BOOT_DELAY_MS           1000U
#define WIFI_RESPONSE_WAIT_MS        4500U

static u8 g_wifi_ready = 0U;

static void WIFI_WaitMs(u32 timeout_ms)
{
    while (timeout_ms >= 1000U)
    {
        delay_ms(1000U);
        timeout_ms -= 1000U;
    }

    if (timeout_ms > 0U)
    {
        delay_ms((u16)timeout_ms);
    }
}

static u8 WIFI_WaitForResponse(const char *expect_primary, const char *expect_secondary)
{
    WIFI_WaitMs(WIFI_RESPONSE_WAIT_MS);

    if ((expect_primary != 0) && (UART2_BufferContains(expect_primary) != 0U))
    {
        return 1U;
    }

    if ((expect_secondary != 0) && (UART2_BufferContains(expect_secondary) != 0U))
    {
        return 1U;
    }

    return 0U;
}

static u8 WIFI_SendCommandWithRetry(const char *command,
                                    const char *expect_primary,
                                    const char *expect_secondary,
                                    const char *step_name)
{
    u8 retry;

    for (retry = 0U; retry < WIFI_CMD_RETRY_COUNT; retry++)
    {
        UART2_ClearBuffer();
        USART2_SendString(command);

        if (WIFI_WaitForResponse(expect_primary, expect_secondary) != 0U)
        {
            printf("WIFI:%s OK\r\n", step_name);
            return 1U;
        }
    }

    printf("ERR:WIFI_%s_FAIL\r\n", step_name);
    return 0U;
}

void WIFI_InitHardware(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(WIFI_EN_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = WIFI_EN_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WIFI_EN_GPIO_PORT, &GPIO_InitStructure);

    g_wifi_ready = 0U;
    WIFI_EN = 0U;
    delay_ms(WIFI_POWER_CYCLE_DELAY_MS);
    WIFI_EN = 1U;
    WIFI_WaitMs(WIFI_BOOT_DELAY_MS);
    UART2_ClearBuffer();
}

u8 WIFI_Connect(void)
{
    g_wifi_ready = 0U;
    printf("WIFI:INIT\r\n");

    if (WIFI_SendCommandWithRetry(WIFI_MODE_CMD, "OK", 0, "MODE") == 0U)
    {
        return 0U;
    }

    if (WIFI_SendCommandWithRetry(WIFI_JOIN_CMD, "WIFI_CONNECT", "OK", "JOIN") == 0U)
    {
        return 0U;
    }

    if (WIFI_SendCommandWithRetry(WIFI_SOCKET_CMD, "OK", 0, "SOCKET") == 0U)
    {
        return 0U;
    }

    if (WIFI_SendCommandWithRetry(WIFI_TRANSPARENT_CMD, ">", 0, "TRANSPARENT") == 0U)
    {
        return 0U;
    }

    g_wifi_ready = 1U;
    printf("WIFI:READY\r\n");
    return 1U;
}

u8 WIFI_SendText(const char *text)
{
    if ((g_wifi_ready == 0U) || (text == 0) || (*text == '\0'))
    {
        return 0U;
    }

    if ((UART2_BufferContains("CLOSED") != 0U) ||
        (UART2_BufferContains("ERROR") != 0U))
    {
        g_wifi_ready = 0U;
        return 0U;
    }

    USART2_SendString(text);
    return 1U;
}

u8 WIFI_IsReady(void)
{
    return g_wifi_ready;
}
