#ifndef __BSP_LOG_H__
#define __BSP_LOG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_LEVEL_E  0
#define LOG_LEVEL_W  1
#define LOG_LEVEL_I  2
#define LOG_LEVEL_D  3

#ifndef LOG_LEVEL
#define LOG_LEVEL   LOG_LEVEL_I
#endif

void bsp_log_init(void);
void bsp_log_write(const char *tag, int lvl, const char *fmt, ...);

#define LOGE(tag, ...)  do { if (LOG_LEVEL >= LOG_LEVEL_E) bsp_log_write(tag, LOG_LEVEL_E, __VA_ARGS__); } while (0)
#define LOGW(tag, ...)  do { if (LOG_LEVEL >= LOG_LEVEL_W) bsp_log_write(tag, LOG_LEVEL_W, __VA_ARGS__); } while (0)
#define LOGI(tag, ...)  do { if (LOG_LEVEL >= LOG_LEVEL_I) bsp_log_write(tag, LOG_LEVEL_I, __VA_ARGS__); } while (0)
#define LOGD(tag, ...)  do { if (LOG_LEVEL >= LOG_LEVEL_D) bsp_log_write(tag, LOG_LEVEL_D, __VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif
