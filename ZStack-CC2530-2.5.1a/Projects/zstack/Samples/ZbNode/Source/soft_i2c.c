/*
 * E18-MS1 板载 OLED 软件 I2C：与 GwCoord 侧 soft_i2c.c 完全同源。
 * cc2530_board.md §4.8 / 原理图 U10：
 *   U10.3 = SCL → P1.2  → OLED_SCL_BM = 0x04
 *   U10.4 = SDA → P1.3  → OLED_SDA_BM = 0x08
 *
 * 本模块用 "开漏模拟 + CC2530 内部上拉" 的方式做主控：
 *   - 释放（线为高）= 切回输入，内部 ~35 kΩ 上拉把 SCL/SDA 拉到 VCC；
 *   - 驱动低         = 切成输出（ODR 位一直保持 0）拉到 GND。
 * 这样即使 E18-MS1 板上没有外部上拉电阻，总线也能正常工作；如果板上另有
 * 外部上拉，两套上拉并联后上升沿更锐利，速率更稳。
 *
 * 延时：内部上拉搭配 ~50 pF 总线电容的典型上升时间 ~4 µs，因此 i2c_delay
 * 至少给到 5 µs 才能保证 SCL 升沿稳到 VIH 之后 OLED 再采样。32 MHz 下约
 * 需要 ~160 cycle NOP。
 */

#include "soft_i2c.h"

#include "ioCC2530.h"
#include "OnBoard.h"

#define OLED_SCL_BM  ((uint8)0x04u)   /* P1.2 */
#define OLED_SDA_BM  ((uint8)0x08u)   /* P1.3 */

static void i2c_delay(void)
{
    /* ~5 µs @ 32 MHz（for 循环本身每迭代 ~3 cycle，加 NOP ≈ 4 cycle = 125 ns）。
     * 太短会让内部上拉来不及把线拉高，太长会拉低 OLED 刷新速率；5 µs 折中。 */
    volatile uint8 i;
    for (i = 0; i < 40u; ++i) {
        asm("NOP");
    }
}

/* 释放 = 切输入，内部上拉把线拉高 */
static void sda_release(void)   { P1DIR &= (uint8)~OLED_SDA_BM; }
static void scl_release(void)   { P1DIR &= (uint8)~OLED_SCL_BM; }

/* 驱动低 = 切输出，ODR 位在 init 里已清 0，于是 pin = 0 */
static void sda_drive_low(void) { P1DIR |= OLED_SDA_BM; }
static void scl_drive_low(void) { P1DIR |= OLED_SCL_BM; }

static void sda_set(uint8 high) { if (high) sda_release(); else sda_drive_low(); }
static void scl_set(uint8 high) { if (high) scl_release(); else scl_drive_low(); }

void soft_i2c_init(void)
{
    /* GPIO 功能，不走 Timer/USART 复用 */
    P1SEL &= (uint8)~(OLED_SDA_BM | OLED_SCL_BM);

    /* P1.2 / P1.3 输入模式使用"上拉/下拉" 而非高阻（P1INP 对应位 = 0） */
    P1INP &= (uint8)~(OLED_SDA_BM | OLED_SCL_BM);

    /* P1 port 的上拉方向 = 上（PDUP1 = 0，P2INP bit6） */
    P2INP &= (uint8)~0x40u;

    /* ODR 位预清 0，这样以后 P1DIR |= mask 立刻驱动低电平 */
    P1 &= (uint8)~(OLED_SDA_BM | OLED_SCL_BM);

    /* 初态释放（高电平，由内部上拉拽高） */
    sda_release();
    scl_release();
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

    /* 释放 SDA 让从机 ACK；本工程为单主 + 无 ACK 检测，保持与 STM32 侧一致。 */
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
