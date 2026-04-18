#include "soft_i2c.h"

#include "ioCC2530.h"
#include "OnBoard.h"

/* cc2530_board.md §4.8 / 原理图 U10：
 *   U10.3 = SCL → P1.2  → OLED_SCL_BM = 0x04
 *   U10.4 = SDA → P1.3  → OLED_SDA_BM = 0x08
 * 如果后续排线改布请同步改这两行。
 */
#define OLED_SCL_BM  ((uint8)0x04u)   /* P1.2 */
#define OLED_SDA_BM  ((uint8)0x08u)   /* P1.3 */

/* 32 MHz CC2530 下每个 NOP ≈ 31 ns，20~40 个 NOP ≈ 1 µs，
 * 对应 100~400 kHz I2C 的最低半周期要求。
 * MicroWait(1) 调用开销已 >200 ns，直接用 NOP 节流更稳。 */
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
    /* 保证 P1.2/P1.3 以 GPIO 输出使用，不被 USART0-Alt2 等外设挂走。 */
    P1SEL &= (uint8)~(OLED_SDA_BM | OLED_SCL_BM);
    P1DIR |= (OLED_SDA_BM | OLED_SCL_BM);
    /* 关闭 P1 的内部下拉（默认可能是输入下拉状态），避免推挽前瞬间被拉低。 */
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
