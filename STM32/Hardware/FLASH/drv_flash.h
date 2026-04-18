#ifndef __DRV_FLASH_H__
#define __DRV_FLASH_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 末页配置存储：运行时根据 flash 容量探测（RBT6=128K / RCT6=256K）。 */
uint32_t drv_flash_config_page_addr(void);
uint16_t drv_flash_page_size(void);

int drv_flash_erase_page(uint32_t addr);              /* 0=OK */
int drv_flash_write(uint32_t addr, const void *data, uint16_t n);
int drv_flash_read(uint32_t addr, void *out, uint16_t n);

uint32_t crc32_ieee(const uint8_t *data, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif
