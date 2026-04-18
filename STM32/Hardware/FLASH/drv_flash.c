#include "drv_flash.h"
#include "stm32f10x.h"
#include <string.h>

uint16_t drv_flash_page_size(void)
{
    /* F103 中密度以上（RBT6/RCT6）页大小 2KB */
    return 2048;
}

uint32_t drv_flash_config_page_addr(void)
{
    /* 读 F_SIZE（FLASH 容量寄存器，单位 KB），0x1FFF F7E0 */
    uint16_t kbs = *(volatile uint16_t *)0x1FFFF7E0;
    uint32_t end;
    if (kbs == 0xFFFF || kbs == 0) kbs = 128;    /* 回退 RBT6 */
    end = 0x08000000u + (uint32_t)kbs * 1024u;
    return end - drv_flash_page_size();
}

int drv_flash_erase_page(uint32_t addr)
{
    FLASH_Status st;
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    st = FLASH_ErasePage(addr);
    FLASH_Lock();
    return (st == FLASH_COMPLETE) ? 0 : -1;
}

int drv_flash_write(uint32_t addr, const void *data, uint16_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t i;
    FLASH_Status st = FLASH_COMPLETE;

    if ((addr & 1) || (n & 1)) return -1;       /* 要求对齐到 half-word */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    for (i = 0; i < n; i += 2) {
        uint16_t hw = (uint16_t)p[i] | ((uint16_t)p[i + 1] << 8);
        st = FLASH_ProgramHalfWord(addr + i, hw);
        if (st != FLASH_COMPLETE) break;
    }
    FLASH_Lock();
    return (st == FLASH_COMPLETE) ? 0 : -1;
}

int drv_flash_read(uint32_t addr, void *out, uint16_t n)
{
    memcpy(out, (const void *)addr, n);
    return 0;
}

uint32_t crc32_ieee(const uint8_t *data, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    uint32_t i;
    uint8_t j;
    for (i = 0; i < n; ++i) {
        c ^= data[i];
        for (j = 0; j < 8; ++j) {
            c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
        }
    }
    return c ^ 0xFFFFFFFFu;
}
