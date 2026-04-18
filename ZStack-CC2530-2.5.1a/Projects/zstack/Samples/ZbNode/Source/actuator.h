#ifndef __ACTUATOR_H__
#define __ACTUATOR_H__

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void actuator_init(void);
void actuator_led_set(uint8 onoff);       /* EP1 */
void actuator_buzzer_set(uint8 onoff);    /* EP2 */
uint8 actuator_led_state(void);
uint8 actuator_buzzer_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __ACTUATOR_H__ */
