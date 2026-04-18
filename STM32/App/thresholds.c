/*
 * 阈值持久化（§5.6）：Flash 末页 2KB 存 thresholds_t + CRC32。
 */

#include "thresholds.h"
#include "config.h"
#include "err.h"
#include "drv_flash.h"
#include <string.h>

typedef struct {
    uint32_t     magic;             /* "ZNJJ" = 0x4A4A4E5A */
    uint32_t     version;
    thresholds_t th;
    uint8_t      pad[4];            /* half-word 对齐 */
    uint32_t     crc32;
} cfg_blob_t;

#define CFG_MAGIC    0x4A4A4E5Au
#define CFG_VERSION  1u

void config_default(thresholds_t *out)
{
    out->lux_low              = TH_LUX_LOW_DEFAULT;
    out->temp_high_x100       = TH_TEMP_HIGH_X100_DEFAULT;
    out->temp_low_x100        = TH_TEMP_LOW_X100_DEFAULT;
    out->hum_high_x100        = TH_HUM_HIGH_X100_DEFAULT;
    out->hum_low_x100         = TH_HUM_LOW_X100_DEFAULT;
    out->hysteresis_lux       = TH_HYSTERESIS_LUX_DEFAULT;
    out->hysteresis_temp_x100 = TH_HYSTERESIS_TEMP_X100_DEF;
    out->debounce_ms          = TH_DEBOUNCE_MS_DEFAULT;
}

int config_load(thresholds_t *out)
{
    cfg_blob_t blob;
    uint32_t crc;
    if (out == 0) return RC_ERR_PARAM;
    drv_flash_read(drv_flash_config_page_addr(), &blob, sizeof(blob));
    if (blob.magic != CFG_MAGIC || blob.version != CFG_VERSION) {
        config_default(out);
        return RC_ERR_NV;
    }
    crc = crc32_ieee((const uint8_t *)&blob, sizeof(blob) - sizeof(uint32_t));
    if (crc != blob.crc32) {
        config_default(out);
        return RC_ERR_NV;
    }
    *out = blob.th;
    return 0;
}

int config_save(const thresholds_t *in)
{
    cfg_blob_t blob;
    if (in == 0) return RC_ERR_PARAM;
    memset(&blob, 0, sizeof(blob));
    blob.magic   = CFG_MAGIC;
    blob.version = CFG_VERSION;
    blob.th      = *in;
    blob.crc32   = crc32_ieee((const uint8_t *)&blob, sizeof(blob) - sizeof(uint32_t));
    if (drv_flash_erase_page(drv_flash_config_page_addr()) != 0) return RC_ERR_NV;
    if (drv_flash_write(drv_flash_config_page_addr(), &blob, sizeof(blob)) != 0) return RC_ERR_NV;
    return 0;
}
