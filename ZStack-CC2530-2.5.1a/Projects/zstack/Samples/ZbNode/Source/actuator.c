/*
 * 执行器：LED + Buzzer（E18-MS1，board_e18ms1.h）。
 * CC2530 I/O 寄存器位访问采用 P1_x / P1DIR 风格（IAR SFR bit 宏）。
 */

#include "actuator.h"
#include "board_e18ms1.h"
#include "ioCC2530.h"

static uint8 s_led = 0;
static uint8 s_buz = 0;

#define SETBIT(reg, b)   ((reg) |=  (1 << (b)))
#define CLRBIT(reg, b)   ((reg) &= ~(1 << (b)))

static void pin_output(uint8 port, uint8 pin)
{
    if (port == 0) { P0SEL &= (uint8)~(1 << pin); P0DIR |= (uint8)(1 << pin); }
    else if (port == 1) { P1SEL &= (uint8)~(1 << pin); P1DIR |= (uint8)(1 << pin); }
    else if (port == 2) { P2SEL &= (uint8)~(1 << pin); P2DIR |= (uint8)(1 << pin); }
}

static void pin_write(uint8 port, uint8 pin, uint8 v)
{
    uint8 m = (uint8)(1 << pin);
    if (port == 0) { if (v) P0 |= m; else P0 &= (uint8)~m; }
    else if (port == 1) { if (v) P1 |= m; else P1 &= (uint8)~m; }
    else if (port == 2) { if (v) P2 |= m; else P2 &= (uint8)~m; }
}

void actuator_init(void)
{
    pin_output(NODE_LED_PORT, NODE_LED_PIN);
    pin_output(NODE_BUZZER_PORT, NODE_BUZZER_PIN);
    actuator_led_set(0);
    actuator_buzzer_set(0);
}

void actuator_led_set(uint8 onoff)
{
    uint8 level = onoff ? NODE_LED_ACTIVE_HIGH : (uint8)(!NODE_LED_ACTIVE_HIGH);
    pin_write(NODE_LED_PORT, NODE_LED_PIN, level);
    s_led = onoff ? 1 : 0;
}

void actuator_buzzer_set(uint8 onoff)
{
    uint8 level = onoff ? NODE_BUZZER_ACTIVE_HIGH : (uint8)(!NODE_BUZZER_ACTIVE_HIGH);
    pin_write(NODE_BUZZER_PORT, NODE_BUZZER_PIN, level);
    s_buz = onoff ? 1 : 0;
}

uint8 actuator_led_state(void)    { return s_led; }
uint8 actuator_buzzer_state(void) { return s_buz; }
