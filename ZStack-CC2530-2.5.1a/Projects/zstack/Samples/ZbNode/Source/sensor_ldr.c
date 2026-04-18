/*
 * 光敏 ADC 采样（AIN7 = P0.7，E18-MS1 板内部分压节点）。
 * 返回值 uint16 lux 估算：ADC code → 3.3V 参考下的近似换算，非精确。
 */

#include "sensor_ldr.h"
#include "board_e18ms1.h"
#include "ioCC2530.h"
#include "hal_adc.h"

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

    /* 线性近似：code 0 ≈ 0 lux（全遮光）；code 2047 ≈ 2000 lux（满量程） */
    lux = ((uint32)code * 2000u) / 2047u;
    if (lux > 65535u) lux = 65535u;
    return (uint16)lux;
}
