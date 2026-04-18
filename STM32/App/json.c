/*
 * 极简 JSON：只支持平坦对象的 key/value 抽取与拼装。
 * 不依赖 cJSON；栈分配；输入指针可含或不含 '\0'（用 jlen 界定）。
 */

#include "json.h"
#include "err.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *find_key(const char *json, uint16_t jlen, const char *key)
{
    uint16_t klen = (uint16_t)strlen(key);
    uint16_t i;
    for (i = 0; i + klen + 3 < jlen; ++i) {
        if (json[i] == '"' &&
            memcmp(&json[i + 1], key, klen) == 0 &&
            json[i + 1 + klen] == '"') {
            /* 跳到冒号后 */
            uint16_t p = (uint16_t)(i + 2 + klen);
            while (p < jlen && (json[p] == ' ' || json[p] == ':')) p++;
            return &json[p];
        }
    }
    return 0;
}

int json_get_string(const char *json, uint16_t jlen, const char *key,
                    char *out, uint16_t cap)
{
    const char *p = find_key(json, jlen, key);
    uint16_t j = 0;
    if (p == 0 || *p != '"') return RC_ERR_BAD_JSON;
    p++;
    while ((uint16_t)(p - json) < jlen && *p != '"' && j + 1 < cap) {
        out[j++] = *p++;
    }
    out[j] = '\0';
    return (*p == '"') ? 0 : RC_ERR_BAD_JSON;
}

int json_get_int(const char *json, uint16_t jlen, const char *key, int32_t *out)
{
    const char *p = find_key(json, jlen, key);
    char *end;
    long v;
    if (p == 0) return RC_ERR_BAD_JSON;
    v = strtol(p, &end, 10);
    if (end == p) return RC_ERR_BAD_JSON;
    *out = (int32_t)v;
    return 0;
}

int json_get_uint(const char *json, uint16_t jlen, const char *key, uint32_t *out)
{
    int32_t v;
    int rc = json_get_int(json, jlen, key, &v);
    if (rc) return rc;
    if (v < 0) return RC_ERR_BAD_JSON;
    *out = (uint32_t)v;
    return 0;
}

int json_get_float(const char *json, uint16_t jlen, const char *key, float *out)
{
    const char *p = find_key(json, jlen, key);
    char *end;
    double v;
    if (p == 0) return RC_ERR_BAD_JSON;
    v = strtod(p, &end);
    if (end == p) return RC_ERR_BAD_JSON;
    *out = (float)v;
    return 0;
}

/* ---------- builder ---------- */
int json_begin(char *buf, uint16_t cap)
{
    if (cap < 2) return RC_ERR_NO_SPACE;
    buf[0] = '{'; buf[1] = '\0';
    return 1;
}

int json_end(char *buf, uint16_t cap, uint16_t cur)
{
    if (cur + 2 > cap) return RC_ERR_NO_SPACE;
    /* 去掉可能的尾逗号 */
    if (cur > 1 && buf[cur - 1] == ',') cur--;
    buf[cur++] = '}';
    buf[cur] = '\0';
    return (int)cur;
}

static int append(char *buf, uint16_t cap, uint16_t cur, const char *src, uint16_t n)
{
    if (cur + n + 1 > cap) return RC_ERR_NO_SPACE;
    memcpy(&buf[cur], src, n);
    buf[cur + n] = '\0';
    return (int)(cur + n);
}

int json_add_str(char *buf, uint16_t cap, uint16_t cur, const char *k, const char *v)
{
    int rc;
    char tmp[96];
    int n = snprintf(tmp, sizeof(tmp), "\"%s\":\"%s\",", k, v);
    if (n < 0 || n >= (int)sizeof(tmp)) return RC_ERR_NO_SPACE;
    rc = append(buf, cap, cur, tmp, (uint16_t)n);
    return rc;
}

int json_add_int(char *buf, uint16_t cap, uint16_t cur, const char *k, int32_t v)
{
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "\"%s\":%ld,", k, (long)v);
    if (n < 0 || n >= (int)sizeof(tmp)) return RC_ERR_NO_SPACE;
    return append(buf, cap, cur, tmp, (uint16_t)n);
}

int json_add_uint(char *buf, uint16_t cap, uint16_t cur, const char *k, uint32_t v)
{
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "\"%s\":%lu,", k, (unsigned long)v);
    if (n < 0 || n >= (int)sizeof(tmp)) return RC_ERR_NO_SPACE;
    return append(buf, cap, cur, tmp, (uint16_t)n);
}

int json_add_bool(char *buf, uint16_t cap, uint16_t cur, const char *k, uint8_t v)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "\"%s\":%s,", k, v ? "true" : "false");
    if (n < 0 || n >= (int)sizeof(tmp)) return RC_ERR_NO_SPACE;
    return append(buf, cap, cur, tmp, (uint16_t)n);
}

int json_add_fp2(char *buf, uint16_t cap, uint16_t cur, const char *k, int32_t x100)
{
    char tmp[64];
    int32_t whole;
    int32_t frac;
    int n;
    const char *sign = "";
    whole = x100 / 100;
    frac  = x100 % 100;
    if (frac < 0) frac = -frac;
    /* 当整数部分为 0 但原值为负（例如 -0.50°C）时，%ld 会丢掉负号。 */
    if (x100 < 0 && whole == 0) sign = "-";
    n = snprintf(tmp, sizeof(tmp), "\"%s\":%s%ld.%02ld,", k, sign, (long)whole, (long)frac);
    if (n < 0 || n >= (int)sizeof(tmp)) return RC_ERR_NO_SPACE;
    return append(buf, cap, cur, tmp, (uint16_t)n);
}
