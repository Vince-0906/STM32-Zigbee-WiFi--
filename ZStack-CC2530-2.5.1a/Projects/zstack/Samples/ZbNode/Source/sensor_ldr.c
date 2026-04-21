/*
 * 光敏 ADC 采样（AIN7 = P0.7，E18-MS1 板内部分压节点）。
 * 返回值为“亮度 lux”估算值，数值越大表示越亮；当前换算仅用于联动和显示，非精确照度计量。
 */

#include "sensor_ldr.h"
#include "board_e18ms1.h"
#include "ioCC2530.h"
#include "hal_adc.h"

#define LDR_ADC_MAX_CODE  2047u
#define LDR_LUX_MAX       2000u

void ldr_init(void)
{
    /* P0.7 设为外设模式（ADC 由 ADCCON 配置通道即可占管） */
    P0SEL |= (uint8)(1 << NODE_LDR_ADC_CH);
    P0DIR &= (uint8)~(1 << NODE_LDR_ADC_CH);
    APCFG |= (uint8)(1 << NODE_LDR_ADC_CH);
}

uint16 ldr_read_lux(void)
{
    uint16 code;
    uint32 lux;

    /* HalAdcRead(ch=HAL_ADC_CHN_AIN7, resolution=HAL_ADC_RESOLUTION_12) 返回 int16（有符号） */
    code = (uint16)HalAdcRead(HAL_ADC_CHN_AIN7, HAL_ADC_RESOLUTION_12);

    if (code > LDR_ADC_MAX_CODE) {
        code = LDR_ADC_MAX_CODE;
    }

    /* 该板分压方向实测为“越暗 ADC 码值越大”，因此此处需反向换算成“越亮 lux 越大”。 */
    lux = ((uint32)(LDR_ADC_MAX_CODE - code) * LDR_LUX_MAX) / LDR_ADC_MAX_CODE;
    return (uint16)lux;
}
