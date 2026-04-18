#include "soft_i2c.h"

#include "ioCC2530.h"

#define OLED_SDA_BM  ((uint8)0x04u)
#define OLED_SCL_BM  ((uint8)0x08u)

static void i2c_delay(void)
{
    volatile uint8 i;
    for (i = 0; i < 8; ++i) {
        ;
    }
}

static void sda_set(uint8 high)
{
    if (high) {
        P1 |= OLED_SDA_BM;
    } else {
        P1 &= (uint8)~OLED_SDA_BM;
    }
}

static void scl_set(uint8 high)
{
    if (high) {
        P1 |= OLED_SCL_BM;
    } else {
        P1 &= (uint8)~OLED_SCL_BM;
    }
}

void soft_i2c_init(void)
{
    P1SEL &= (uint8)~(OLED_SDA_BM | OLED_SCL_BM);
    P1DIR |= (OLED_SDA_BM | OLED_SCL_BM);
    sda_set(1);
    scl_set(1);
}

void soft_i2c_start(void)
{
    sda_set(1);
    scl_set(1);
    i2c_delay();
    sda_set(0);
    i2c_delay();
    scl_set(0);
}

void soft_i2c_stop(void)
{
    sda_set(0);
    i2c_delay();
    scl_set(1);
    i2c_delay();
    sda_set(1);
    i2c_delay();
}

uint8 soft_i2c_write_byte(uint8 data)
{
    uint8 i;

    for (i = 0; i < 8u; ++i) {
        sda_set((data & 0x80u) ? 1u : 0u);
        i2c_delay();
        scl_set(1);
        i2c_delay();
        scl_set(0);
        data <<= 1;
    }

    sda_set(1);
    i2c_delay();
    scl_set(1);
    i2c_delay();
    scl_set(0);
    return 1;
}
