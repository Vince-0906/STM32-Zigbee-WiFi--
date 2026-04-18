/*
 * DHT11 单总线驱动（P2.0，10k 上拉）。
 * 时序：主机拉低 ≥18ms → 释放总线 20~40us → 读 40-bit 数据。
 * 禁浮点：温度取 i16 *100（°C*100），湿度 u16 *100 (%*100)。
 */

#include "sensor_dht11.h"
#include "board_e18ms1.h"
#include "ioCC2530.h"
#include "OnBoard.h"

#define DHT11_BIT_MASK  (uint8)(1 << NODE_DHT11_PIN)

static void dht11_pin_out(void)  { P2SEL &= (uint8)~DHT11_BIT_MASK; P2DIR |= DHT11_BIT_MASK; }
static void dht11_pin_in(void)   { P2SEL &= (uint8)~DHT11_BIT_MASK; P2DIR &= (uint8)~DHT11_BIT_MASK; }
static void dht11_set(uint8 v)   { if (v) P2 |= DHT11_BIT_MASK; else P2 &= (uint8)~DHT11_BIT_MASK; }
static uint8 dht11_get(void)     { return (uint8)((P2 & DHT11_BIT_MASK) ? 1 : 0); }

/* 在 32 MHz 时钟下用简单 for 循环延时（非精确，但 DHT11 容差大）。 */
static void delay_us(uint16 us)
{
    /* 经 32MHz + -Ohigh 经验值：每个 for 迭代 ~1us。上电实测再校准。 */
    volatile uint16 i;
    while (us--) {
        for (i = 0; i < 8; ++i) { ; }
    }
}

static void delay_ms(uint16 ms)
{
    while (ms--) delay_us(1000);
}

void dht11_init(void)
{
    dht11_pin_out();
    dht11_set(1);
}

/* 等 pin 变为 expect 或超时（us） */
static int8 wait_level(uint8 expect, uint16 timeout_us)
{
    while (timeout_us--) {
        if (dht11_get() == expect) return 0;
        delay_us(1);
    }
    return -1;
}

int8 dht11_read(int16 *temp_x100, uint16 *hum_x100)
{
    uint8 data[5];
    uint8 i, j, b;

    if (temp_x100 == 0 || hum_x100 == 0) return -1;

    /* Start: 主机拉低 20ms → 拉高 30us → 切输入 */
    dht11_pin_out();
    dht11_set(0);
    delay_ms(20);
    dht11_set(1);
    delay_us(30);
    dht11_pin_in();

    /* 等 DHT11 应答：先低 80us 再高 80us */
    if (wait_level(0, 100) < 0) return -2;
    if (wait_level(1, 100) < 0) return -3;
    if (wait_level(0, 100) < 0) return -4;

    /* 40 bits */
    for (i = 0; i < 5; ++i) {
        b = 0;
        for (j = 0; j < 8; ++j) {
            /* 每位前 50us 低电平 */
            if (wait_level(1, 100) < 0) return -5;
            delay_us(30);                         /* 约略采样点 */
            b <<= 1;
            if (dht11_get()) b |= 1;
            if (wait_level(0, 100) < 0) return -6;
        }
        data[i] = b;
    }

    /* 校验：data[0..3] 累加 == data[4] */
    if ((uint8)(data[0] + data[1] + data[2] + data[3]) != data[4]) return -7;

    /* DHT11 格式：data[0]=湿度整数，data[1]=湿度小数(通常 0)，data[2]=温度整数，data[3]=温度小数低位=0.1°C */
    *hum_x100  = (uint16)((uint16)data[0] * 100u + (uint16)data[1]);
    *temp_x100 = (int16)((int16)data[2] * 100 + (int16)(data[3] & 0x7F) * 10);
    if (data[3] & 0x80) *temp_x100 = (int16)(-(*temp_x100));
    return 0;
}
