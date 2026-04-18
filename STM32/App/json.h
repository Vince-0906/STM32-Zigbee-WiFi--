#ifndef __PROTO_JSON_H__
#define __PROTO_JSON_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 极简 JSON 抽取器（§4.2 所需字段）。
 * 不做通用解析，仅支持平坦对象 {"k":<v>,...}，v = 字符串 / 整数 / 浮点。
 *
 * 返回 0=OK, <0=not found / parse error。
 */
int json_get_string(const char *json, uint16_t jlen, const char *key,
                    char *out, uint16_t cap);
int json_get_int   (const char *json, uint16_t jlen, const char *key, int32_t *out);
int json_get_uint  (const char *json, uint16_t jlen, const char *key, uint32_t *out);
int json_get_float (const char *json, uint16_t jlen, const char *key, float *out);

/* 简单构造器：附加一个 k:v 对到 buf（自动加逗号）。成功返回新 len，失败 <0。 */
int json_begin(char *buf, uint16_t cap);
int json_end  (char *buf, uint16_t cap, uint16_t cur);

int json_add_str (char *buf, uint16_t cap, uint16_t cur, const char *k, const char *v);
int json_add_int (char *buf, uint16_t cap, uint16_t cur, const char *k, int32_t v);
int json_add_uint(char *buf, uint16_t cap, uint16_t cur, const char *k, uint32_t v);
int json_add_fp2 (char *buf, uint16_t cap, uint16_t cur, const char *k, int32_t x100); /* 输出 x100/100 精度两位 */

#ifdef __cplusplus
}
#endif

#endif
