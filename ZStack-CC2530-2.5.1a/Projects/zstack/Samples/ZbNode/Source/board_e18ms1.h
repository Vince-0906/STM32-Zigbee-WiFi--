#ifndef __BOARD_E18_MS1_H__
#define __BOARD_E18_MS1_H__

/*
 * E18-MS1 开发板 CC2530 引脚映射（参考 references/cc2530_board.md §3）。
 * 该板同时用于 GwCoord 与 ZbNode；节点侧通过 NODE_ROLE 宏区分 Node1/Node2。
 *
 * LED：LED1=P1.4  LED2=P0.1(强复用 H2)  LED3=P1.0  LED4=P1.1
 *      蜂鸣器 P1.7（Q3 驱动，输出高电平鸣叫）
 *      独立按键 SW1=P1.5(复用 ESP RST)  SW2=P1.6
 *      DHT11 DATA=P2.0，10k 上拉
 *      I2C SCL=P1.2 SDA=P1.3（本项目未用）
 *      P0.7 分压节点 → 光敏 ADC 采样
 *
 * 固件使用原则：
 *  - 节点 LED/Buzzer 共用低/高有效逻辑详见 §4.3/§4.7，本项目以"高电平点亮/鸣叫"处理，
 *    上电实测若相反，修改 ACT_ACTIVE_HIGH 宏即可（此处按 cc2530_board §4.7 结论）。
 *  - 协调器的入网/数据灯用 LED3(P1.0)/LED4(P1.1) 对应 HAL_LED_1 / HAL_LED_2。
 *  - 节点侧业务 LED = LED1(P1.4)，Buzzer = P1.7，统一走 actuator.c。
 *  - 节点侧状态灯：LED3(P1.0) 负责启动闪烁提示，LED4(P1.1) 常亮表示节点存活。
 */

/* ---- 节点执行器引脚（0x0006 OnOff EP1=LED, EP2=Buzzer） ---- */
#define NODE_LED_PORT           1       /* P1 */
#define NODE_LED_PIN            4       /* P1.4 = LED1 */

#define NODE_BUZZER_PORT        1
#define NODE_BUZZER_PIN         7       /* P1.7 */

#define NODE_START_LED_PORT     1
#define NODE_START_LED_PIN      0       /* P1.0 = LED3 */

#define NODE_ALIVE_LED_PORT     1
#define NODE_ALIVE_LED_PIN      1       /* P1.1 = LED4 */

/* ---- 传感器 ---- */
#define NODE_DHT11_PORT         2
#define NODE_DHT11_PIN          0       /* P2.0 */

#define NODE_LDR_ADC_CH         7       /* AIN7 = P0.7 */

/* 执行器电平极性（实测若反向改为 0） */
#define ACT_ACTIVE_HIGH         1

/* Node 角色宏（工程 CCFLAGS 传入 -DNODE_ROLE=1 / =2） */
#ifndef NODE_ROLE
#define NODE_ROLE               1
#endif

/* ---- 上报周期 ---- */
#if NODE_ROLE == 1
#define NODE_REPORT_PERIOD_MS   2000u   /* Node1 DHT11 2s */
#else
#define NODE_REPORT_PERIOD_MS   500u    /* Node2 光敏 500ms */
#endif

#endif /* __BOARD_E18_MS1_H__ */
