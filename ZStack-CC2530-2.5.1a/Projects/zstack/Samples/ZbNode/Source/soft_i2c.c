/*
 * E18-MS1 板载 OLED 软件 I2C：与 GwCoord 侧 soft_i2c.c 完全同源。
 * cc2530_board.md §4.8 / 原理图 U10：
 *   U10.3 = SCL → P1.2  → OLED_SCL_BM = 0x04
 *   U10.4 = SDA → P1.3  → OLED_SDA_BM = 0x08
 */

#include "soft_i2c.h"

#include "ioCC2530.h"
#include "OnBoard.h"

#define OLED_SCL_BM  ((uint8)0x04u)   /* P1.2 */
#define OLED_SDA_BM  ((uint8)0x08u)   /* P1.3 */

static void i2c_delay(void)
{
    volatile uint8 i;
    for (i = 0; i < 20u; ++i) {
        asm("NOP");
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
    P2INP &= (uint8)~0x40u;     /* P1 pull select: 0 = pull-up */
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
    i2c_delay();
}

void soft_i2c_stop(void)
{
    scl_set(0);
    i2c_delay();
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
        scl_set(0);
        i2c_delay();
        sda_set((data & 0x80u) ? 1u : 0u);
        i2c_delay();
        scl_set(1);
        i2c_delay();
        data <<= 1;
    }

    scl_set(0);
    i2c_delay();
    sda_set(1);
    i2c_delay();
    scl_set(1);
    i2c_delay();
    scl_set(0);
    i2c_delay();
    return 1;
}
