/*
 * json_builder.c - JSON string escaping and dynamic buffer
 */

#include "mobius/json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* --- Dynamic buffer --- */

#define JSON_BUF_INITIAL_CAP 256

void json_buf_init(json_buf_t *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void json_buf_free(json_buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void json_buf_grow(json_buf_t *buf, size_t needed)
{
    if (buf->len + needed <= buf->cap)
        return;

    size_t new_cap = buf->cap ? buf->cap * 2 : JSON_BUF_INITIAL_CAP;
    while (new_cap < buf->len + needed)
        new_cap *= 2;

    char *new_data = (char *)realloc(buf->data, new_cap);
    if (!new_data)
        return; /* OOM — silent fail, caller checks buf->data */
    buf->data = new_data;
    buf->cap = new_cap;
}

void json_buf_append(json_buf_t *buf, const char *str, size_t len)
{
    if (len == 0) return;
    json_buf_grow(buf, len + 1);
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void json_buf_append_str(json_buf_t *buf, const char *str)
{
    json_buf_append(buf, str, strlen(str));
}

void json_buf_printf(json_buf_t *buf, const char *fmt, ...)
{
    va_list ap;

    /* First pass: measure needed size */
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed <= 0) return;

    json_buf_grow(buf, (size_t)needed + 1);

    va_start(ap, fmt);
    vsnprintf(buf->data + buf->len, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    buf->len += (size_t)needed;
}

/* --- JSON string escaping --- */

static void escape_char(json_buf_t *buf, unsigned char c)
{
    switch (c) {
    case '"':  json_buf_append(buf, "\\\"", 2); break;
    case '\\': json_buf_append(buf, "\\\\", 2); break;
    case '\n': json_buf_append(buf, "\\n", 2);  break;
    case '\r': json_buf_append(buf, "\\r", 2);  break;
    case '\t': json_buf_append(buf, "\\t", 2);  break;
    default:
        if (c < 0x20) {
            /* Control character — \u00XX */
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            json_buf_append(buf, esc, 6);
        } else {
            json_buf_append(buf, (const char *)&c, 1);
        }
        break;
    }
}

void json_buf_append_escaped(json_buf_t *buf, const char *str)
{
    if (!str) return;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        escape_char(buf, *p);
        p++;
    }
}

void json_buf_append_escaped_n(json_buf_t *buf, const char *str, size_t len)
{
    size_t i;
    if (!str) return;
    for (i = 0; i < len; i++)
        escape_char(buf, (unsigned char)str[i]);
}

void json_buf_add_string(json_buf_t *buf, const char *key, const char *value)
{
    json_buf_append_str(buf, "\"");
    json_buf_append_str(buf, key);
    json_buf_append_str(buf, "\": \"");
    json_buf_append_escaped(buf, value);
    json_buf_append_str(buf, "\"");
}

void json_buf_add_int(json_buf_t *buf, const char *key, int value)
{
    json_buf_printf(buf, "\"%s\": %d", key, value);
}

void json_buf_add_bool(json_buf_t *buf, const char *key, int value)
{
    json_buf_printf(buf, "\"%s\": %s", key, value ? "true" : "false");
}

/* --- Standalone escaping into fixed buffer --- */

int json_escape_string(const char *input, char *out_buf, size_t out_size)
{
    if (!input || !out_buf || out_size == 0)
        return -1;

    /* Empty input is valid */
    if (input[0] == '\0') {
        out_buf[0] = '\0';
        return 0;
    }

    json_buf_t tmp;
    json_buf_init(&tmp);
    json_buf_append_escaped(&tmp, input);

    if (!tmp.data) {
        out_buf[0] = '\0';
        return -1;
    }

    size_t copy_len = tmp.len;
    int truncated = 0;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
        truncated = 1;
    }

    memcpy(out_buf, tmp.data, copy_len);
    out_buf[copy_len] = '\0';
    json_buf_free(&tmp);

    return truncated ? -1 : (int)copy_len;
}

/* --- Minimal reading helpers for flat JSON objects --- */

static int json_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

const char *json_find_value(const char *json, const char *key)
{
    char needle[128];
    const char *p;
    const char *q;
    size_t nlen;

    if (!json || !key) return NULL;
    if (snprintf(needle, sizeof(needle), "\"%s\"", key) >= (int)sizeof(needle))
        return NULL;
    nlen = strlen(needle);

    for (p = strstr(json, needle); p; p = strstr(p + 1, needle)) {
        /* A real key is preceded (ignoring whitespace) by '{' or ','.
         * This rejects matches inside string values. */
        q = p;
        while (q > json && json_is_ws(q[-1])) q--;
        if (q != json && q[-1] != '{' && q[-1] != ',') continue;

        q = p + nlen;
        while (json_is_ws(*q)) q++;
        if (*q != ':') continue;
        q++;
        while (json_is_ws(*q)) q++;
        return q;
    }
    return NULL;
}

/* Parse 4 hex digits; returns the value or -1. */
static int json_hex4(const char *p)
{
    int i, v = 0;
    for (i = 0; i < 4; i++) {
        char h = p[i];
        v <<= 4;
        if (h >= '0' && h <= '9') v |= h - '0';
        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        else return -1;
    }
    return v;
}

/* Unescape a JSON string body starting just after its opening quote.
 * If out is NULL, only measures. Returns the unescaped length. */
static size_t json_unescape(const char *p, char *out, size_t out_size)
{
    size_t n = 0;
    char c;

    while (*p && *p != '"') {
        if (*p == '\\' && p[1] == 'u' && json_hex4(p + 2) >= 0 &&
            json_hex4(p + 2) < 0x100) {
            /* \u00XX — what our writers emit for control characters */
            c = (char)json_hex4(p + 2);
            p += 6;
        } else if (*p == '\\' && p[1]) {
            p += 2;
            switch (p[-1]) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case '"': case '\\': case '/': c = p[-1]; break;
            default:
                /* Not a JSON escape (e.g. raw "\|" in ASCII art written by
                 * older builds): keep the backslash literally. */
                c = '\\';
                p--;
                break;
            }
        } else {
            c = *p++;
        }
        if (out && n + 1 < out_size) out[n] = c;
        n++;
    }
    if (out && out_size) out[n < out_size ? n : out_size - 1] = '\0';
    return n;
}

int json_read_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p = json_find_value(json, key);

    if (out_size) out[0] = '\0';
    if (!p || *p != '"') return -1;
    json_unescape(p + 1, out, out_size);
    return 0;
}

char *json_read_string_alloc(const char *json, const char *key)
{
    const char *p = json_find_value(json, key);
    size_t len;
    char *out;

    if (!p || *p != '"') return NULL;
    len = json_unescape(p + 1, NULL, 0);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    json_unescape(p + 1, out, len + 1);
    return out;
}
