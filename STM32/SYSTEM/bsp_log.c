#include "bsp_log.h"
#include "drv_usart.h"
#include "drv_tim.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static char s_buf[160];

void bsp_log_init(void) { /* USART1 已在 drv_usart_init_all() 里起来 */ }

void bsp_log_write(const char *tag, int lvl, const char *fmt, ...)
{
    static const char level_ch[] = "EWID";
    int n;
    va_list ap;

    n = snprintf(s_buf, sizeof(s_buf), "[%lu][%c][%s] ",
                 (unsigned long)ms_now(),
                 level_ch[lvl & 3],
                 tag ? tag : "");
    if (n < 0 || n >= (int)sizeof(s_buf)) return;

    va_start(ap, fmt);
    n += vsnprintf(s_buf + n, sizeof(s_buf) - (size_t)n, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(s_buf) - 2) n = (int)sizeof(s_buf) - 3;

    s_buf[n++] = '\r';
    s_buf[n++] = '\n';
    usart_log_write(s_buf, (uint16_t)n);
}
