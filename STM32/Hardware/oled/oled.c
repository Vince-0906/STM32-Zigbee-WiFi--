#include "oled.h"
#include "oled_font.h"
#include "delay.h"

#define OLED_CHAR_WIDTH     8
#define OLED_CHAR_HEIGHT    16

static u8 OLED_GRAM[OLED_WIDTH][8];

static void OLED_I2C_Delay(void)
{
    u8 i = 5;
    while (i--);
}

static void OLED_I2C_Start(void)
{
    OLED_SDA = 1;
    OLED_SCL = 1;
    OLED_I2C_Delay();
    OLED_SDA = 0;
    OLED_I2C_Delay();
    OLED_SCL = 0;
    OLED_I2C_Delay();
}

static void OLED_I2C_Stop(void)
{
    OLED_SDA = 0;
    OLED_SCL = 1;
    OLED_I2C_Delay();
    OLED_SDA = 1;
    OLED_I2C_Delay();
}

static void OLED_I2C_WaitAck(void)
{
    OLED_SDA = 1;
    OLED_I2C_Delay();
    OLED_SCL = 1;
    OLED_I2C_Delay();
    OLED_SCL = 0;
    OLED_I2C_Delay();
}

static void OLED_I2C_WriteByte(u8 data)
{
    u8 i;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            OLED_SDA = 1;
        }
        else
        {
            OLED_SDA = 0;
        }

        data <<= 1;
        OLED_I2C_Delay();
        OLED_SCL = 1;
        OLED_I2C_Delay();
        OLED_SCL = 0;
        OLED_I2C_Delay();
    }
}

static void OLED_WriteByte(u8 data, u8 mode)
{
    OLED_I2C_Start();
    OLED_I2C_WriteByte(OLED_ADDRESS);
    OLED_I2C_WaitAck();
    OLED_I2C_WriteByte(mode ? 0x40 : 0x00);
    OLED_I2C_WaitAck();
    OLED_I2C_WriteByte(data);
    OLED_I2C_WaitAck();
    OLED_I2C_Stop();
}

static void OLED_ClearPoint(u8 x, u8 y)
{
    u8 page;
    u8 bit;

    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
    {
        return;
    }

    page = y / 8;
    bit = y % 8;
    OLED_GRAM[x][page] &= ~(1 << bit);
}

static void OLED_DrawHLine(u8 x, u8 y, u8 length)
{
    u8 i;

    for (i = 0; i < length; i++)
    {
        OLED_DrawPoint(x + i, y);
    }
}

static void OLED_DrawVLine(u8 x, u8 y, u8 length)
{
    u8 i;

    for (i = 0; i < length; i++)
    {
        OLED_DrawPoint(x, y + i);
    }
}

void OLED_Refresh(void)
{
    u8 page;
    u8 column;

    for (page = 0; page < 8; page++)
    {
        OLED_WriteByte(0xB0 + page, OLED_CMD);
        OLED_WriteByte(0x00, OLED_CMD);
        OLED_WriteByte(0x10, OLED_CMD);

        for (column = 0; column < OLED_WIDTH; column++)
        {
            OLED_WriteByte(OLED_GRAM[column][page], OLED_DATA);
        }
    }
}

void OLED_Clear(void)
{
    u8 page;
    u8 column;

    for (page = 0; page < 8; page++)
    {
        for (column = 0; column < OLED_WIDTH; column++)
        {
            OLED_GRAM[column][page] = 0x00;
        }
    }
}

void OLED_DrawPoint(u8 x, u8 y)
{
    u8 page;
    u8 bit;

    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
    {
        return;
    }

    page = y / 8;
    bit = y % 8;
    OLED_GRAM[x][page] |= (1 << bit);
}

void OLED_ShowChar(u8 x, u8 y, char chr)
{
    u8 column;
    u8 row;
    u8 index;
    u8 data_upper;
    u8 data_lower;

    if ((x > (OLED_WIDTH - OLED_CHAR_WIDTH)) || (y > (OLED_HEIGHT - OLED_CHAR_HEIGHT)))
    {
        return;
    }

    if ((chr < ' ') || (chr > '~'))
    {
        chr = ' ';
    }

    index = (u8)(chr - ' ');

    for (column = 0; column < OLED_CHAR_WIDTH; column++)
    {
        data_upper = asc2_1608[index][column * 2];
        data_lower = asc2_1608[index][column * 2 + 1];

        for (row = 0; row < 8; row++)
        {
            if (data_upper & 0x80)
            {
                OLED_DrawPoint(x + column, y + row);
            }
            else
            {
                OLED_ClearPoint(x + column, y + row);
            }
            data_upper <<= 1;
        }

        for (row = 0; row < 8; row++)
        {
            if (data_lower & 0x80)
            {
                OLED_DrawPoint(x + column, y + 8 + row);
            }
            else
            {
                OLED_ClearPoint(x + column, y + 8 + row);
            }
            data_lower <<= 1;
        }
    }
}

void OLED_ShowString(u8 x, u8 y, const char *str)
{
    while ((*str != '\0') && (x <= (OLED_WIDTH - OLED_CHAR_WIDTH)))
    {
        OLED_ShowChar(x, y, *str);
        x += OLED_CHAR_WIDTH;
        str++;
    }
}

void OLED_ShowNum(u8 x, u8 y, u32 num, u8 len)
{
    u8 i;
    u32 divisor = 1;

    for (i = 1; i < len; i++)
    {
        divisor *= 10;
    }

    for (i = 0; i < len; i++)
    {
        OLED_ShowChar(x + i * OLED_CHAR_WIDTH, y, (char)('0' + ((num / divisor) % 10)));
        if (divisor > 1)
        {
            divisor /= 10;
        }
    }
}

void OLED_DrawProgressBar(u8 x, u8 y, u8 width, u8 height, u8 percent)
{
    u8 fill_width;
    u8 i;
    u8 j;

    if ((width < 2) || (height < 2) || (x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
    {
        return;
    }

    if ((x + width) > OLED_WIDTH)
    {
        width = OLED_WIDTH - x;
    }

    if ((y + height) > OLED_HEIGHT)
    {
        height = OLED_HEIGHT - y;
    }

    if (percent > 100)
    {
        percent = 100;
    }

    OLED_DrawHLine(x, y, width);
    OLED_DrawHLine(x, y + height - 1, width);
    OLED_DrawVLine(x, y, height);
    OLED_DrawVLine(x + width - 1, y, height);

    fill_width = (u8)(((u16)(width - 2) * percent) / 100U);

    for (i = 1; i < (width - 1); i++)
    {
        for (j = 1; j < (height - 1); j++)
        {
            if (i <= fill_width)
            {
                OLED_DrawPoint(x + i, y + j);
            }
            else
            {
                OLED_ClearPoint(x + i, y + j);
            }
        }
    }
}

void OLED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_SDA_PIN | OLED_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    OLED_SDA = 1;
    OLED_SCL = 1;

    delay_ms(100);

    OLED_WriteByte(0xAE, OLED_CMD);
    OLED_WriteByte(0x00, OLED_CMD);
    OLED_WriteByte(0x10, OLED_CMD);
    OLED_WriteByte(0x40, OLED_CMD);
    OLED_WriteByte(0x81, OLED_CMD);
    OLED_WriteByte(0xCF, OLED_CMD);
    OLED_WriteByte(0xA1, OLED_CMD);
    OLED_WriteByte(0xC8, OLED_CMD);
    OLED_WriteByte(0xA6, OLED_CMD);
    OLED_WriteByte(0xA8, OLED_CMD);
    OLED_WriteByte(0x3F, OLED_CMD);
    OLED_WriteByte(0xD3, OLED_CMD);
    OLED_WriteByte(0x00, OLED_CMD);
    OLED_WriteByte(0xD5, OLED_CMD);
    OLED_WriteByte(0x80, OLED_CMD);
    OLED_WriteByte(0xD9, OLED_CMD);
    OLED_WriteByte(0xF1, OLED_CMD);
    OLED_WriteByte(0xDA, OLED_CMD);
    OLED_WriteByte(0x12, OLED_CMD);
    OLED_WriteByte(0xDB, OLED_CMD);
    OLED_WriteByte(0x40, OLED_CMD);
    OLED_WriteByte(0x20, OLED_CMD);
    OLED_WriteByte(0x02, OLED_CMD);
    OLED_WriteByte(0x8D, OLED_CMD);
    OLED_WriteByte(0x14, OLED_CMD);
    OLED_WriteByte(0xA4, OLED_CMD);
    OLED_WriteByte(0xA6, OLED_CMD);
    OLED_WriteByte(0xAF, OLED_CMD);

    OLED_Clear();
    OLED_Refresh();
}
