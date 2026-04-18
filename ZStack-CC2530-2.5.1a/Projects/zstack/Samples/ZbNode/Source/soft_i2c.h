#ifndef __ZBNODE_SOFT_I2C_H__
#define __ZBNODE_SOFT_I2C_H__

#include "ZComDef.h"

void  soft_i2c_init(void);
void  soft_i2c_start(void);
void  soft_i2c_stop(void);
uint8 soft_i2c_write_byte(uint8 data);

#endif
