/*
 * json_builder.h - JSON string escaping and dynamic buffer for building payloads
 *
 * Used by Mnemosyne sync to build JSON POSTs without a JSON library.
 */

#ifndef MOBIUS_JSON_BUILDER_H
#define MOBIUS_JSON_BUILDER_H

#include <stddef.h>
#include <stdint.h>

/* --- Dynamic buffer for building JSON payloads --- */

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} json_buf_t;

/* Initialize a buffer (call before use, or zero-init is fine) */
void json_buf_init(json_buf_t *buf);

/* Free the buffer's memory */
void json_buf_free(json_buf_t *buf);

/* Append raw bytes */
void json_buf_append(json_buf_t *buf, const char *str, size_t len);

/* Append a null-terminated string */
void json_buf_append_str(json_buf_t *buf, const char *str);

/* Append formatted output (printf-style) */
void json_buf_printf(json_buf_t *buf, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Append a JSON-escaped string (without surrounding quotes) */
void json_buf_append_escaped(json_buf_t *buf, const char *str);

/* Append len bytes of str JSON-escaped (without surrounding quotes) */
void json_buf_append_escaped_n(json_buf_t *buf, const char *str, size_t len);

/* Convenience: append "key": "escaped_value" */
void json_buf_add_string(json_buf_t *buf, const char *key, const char *value);

/* Convenience: append "key": number */
void json_buf_add_int(json_buf_t *buf, const char *key, int value);

/* Convenience: append "key": true/false */
void json_buf_add_bool(json_buf_t *buf, const char *key, int value);

/* --- Standalone JSON string escaping --- */

/*
 * json_escape_string - Escape a string for JSON embedding.
 *
 * Handles: \" \\ \n \r \t and control characters (\\u00XX).
 * Writes into out_buf (up to out_size bytes including null terminator).
 * Returns the number of characters written (excluding null), or -1 if
 * the buffer was too small (output is still null-terminated but truncated).
 */
int json_escape_string(const char *input, char *out_buf, size_t out_size);

/* --- Minimal reading helpers for flat JSON objects --- */

/*
 * json_find_value - Locate the value for "key" in a flat JSON object.
 *
 * Accepts any whitespace around the colon, so it reads both the compact
 * form ("key":"v") and the form json_buf_add_* writes ("key": "v").
 * Occurrences of "key" inside string values are skipped (their quotes are
 * escaped). Returns a pointer to the first non-whitespace character of the
 * value, or NULL if the key is absent.
 */
const char *json_find_value(const char *json, const char *key);

/*
 * json_read_string - Copy and unescape the string value of "key" into out.
 *
 * Handles \" \\ \/ \n \r \t \b \f. Output is always NUL-terminated and
 * truncated to out_size - 1 bytes. Returns 0 if the key held a string,
 * -1 otherwise (out is then set to "").
 */
int json_read_string(const char *json, const char *key, char *out, size_t out_size);

/*
 * json_read_string_alloc - Like json_read_string, but returns a malloc'd
 * copy of any length. Returns NULL if the key is absent or not a string.
 */
char *json_read_string_alloc(const char *json, const char *key);

#endif /* MOBIUS_JSON_BUILDER_H */
