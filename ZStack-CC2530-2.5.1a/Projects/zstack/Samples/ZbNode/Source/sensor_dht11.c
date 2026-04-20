/*
 * DHT11 single-wire driver on CC2530 P2.0.
 *
 * The board provides an external 10k pull-up. Follow the reference start
 * sequence: drive low >=18 ms, drive high for 20~40 us, then switch to input
 * to sample the sensor response. Timing assumes the system is already running
 * on the existing 32 MHz XOSC configuration used by the project.
 */

#include "sensor_dht11.h"
#include "board_e18ms1.h"
#include "ioCC2530.h"
#include "hal_mcu.h"

#define DHT11_BIT_MASK  ((uint8)(1u << NODE_DHT11_PIN))

static void dht11_gpio_mode(void)
{
    P2SEL &= (uint8)~DHT11_BIT_MASK;
}

static void dht11_drive_low(void)
{
    dht11_gpio_mode();
    P2 &= (uint8)~DHT11_BIT_MASK;
    P2DIR |= DHT11_BIT_MASK;
}

static void dht11_drive_high(void)
{
    dht11_gpio_mode();
    P2 |= DHT11_BIT_MASK;
    P2DIR |= DHT11_BIT_MASK;
}

static void dht11_input_mode(void)
{
    dht11_gpio_mode();
    P2DIR &= (uint8)~DHT11_BIT_MASK;
}

static uint8 dht11_get(void)
{
    dht11_gpio_mode();
    return (uint8)((P2 & DHT11_BIT_MASK) ? 1u : 0u);
}

static void delay_us(uint16 us)
{
    while (us--) {
        asm("NOP");
        asm("NOP");
        asm("NOP");
        asm("NOP");
        asm("NOP");
        asm("NOP");
        asm("NOP");
        asm("NOP");
    }
}

static void delay_ms(uint16 ms)
{
    while (ms--) {
        delay_us(1000u);
    }
}

void dht11_init(void)
{
    dht11_drive_high();
}

static uint8 wait_level(uint8 expect, uint16 timeout_us)
{
    while (timeout_us--) {
        if (dht11_get() == expect) {
            return 1u;
        }
        delay_us(1u);
    }
    return 0u;
}

int8 dht11_read(int16 *temp_x100, uint16 *hum_x100)
{
    uint8 data[5];
    uint8 i;
    uint8 j;
    uint8 b;
    int8 rc;
    if (temp_x100 == 0 || hum_x100 == 0) {
        return DHT11_ERR_PARAM;
    }

    rc = DHT11_OK;

    dht11_drive_low();
    delay_ms(20u);
    dht11_drive_high();
    delay_us(30u);

    dht11_input_mode();

    if (!wait_level(0u, 100u)) {
        dht11_drive_high();
        return DHT11_ERR_RESP_LOW;
    }
    if (!wait_level(1u, 100u)) {
        dht11_drive_high();
        return DHT11_ERR_RESP_HIGH;
    }
    if (!wait_level(0u, 100u)) {
        dht11_drive_high();
        return DHT11_ERR_RESP_END;
    }

    {
        halIntState_t int_state;

        HAL_ENTER_CRITICAL_SECTION(int_state);
        for (i = 0u; i < 5u; ++i) {
            b = 0u;
            for (j = 0u; j < 8u; ++j) {
                if (!wait_level(1u, 100u)) {
                    HAL_EXIT_CRITICAL_SECTION(int_state);
                    dht11_drive_high();
                    return DHT11_ERR_BIT_HIGH;
                }
                delay_us(30u);
                b <<= 1;
                if (dht11_get()) {
                    b |= 1u;
                }
                if (!wait_level(0u, 100u)) {
                    HAL_EXIT_CRITICAL_SECTION(int_state);
                    dht11_drive_high();
                    return DHT11_ERR_BIT_LOW;
                }
            }
            data[i] = b;
        }
        HAL_EXIT_CRITICAL_SECTION(int_state);
    }

    dht11_drive_high();

    if (rc != DHT11_OK) {
        return rc;
    }

    if ((uint8)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return DHT11_ERR_CHECKSUM;
    }

    *hum_x100 = (uint16)((uint16)data[0] * 100u + (uint16)data[1]);
    *temp_x100 = (int16)((int16)data[2] * 100 + (int16)(data[3] & 0x7Fu) * 10);
    if (data[3] & 0x80u) {
        *temp_x100 = (int16)(-(*temp_x100));
    }

    return DHT11_OK;
}
