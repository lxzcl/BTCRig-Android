/*
 * Minimal Android/NDK public configuration for vendored Jansson.
 */

#ifndef JANSSON_CONFIG_H
#define JANSSON_CONFIG_H

#ifndef JANSSON_USING_CMAKE
#define JANSSON_USING_CMAKE
#endif

#define JSON_INTEGER_IS_LONG_LONG 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1

#if defined(HAVE_STDINT_H)
#include <stdint.h>
#elif defined(HAVE_SYS_TYPES_H)
#include <sys/types.h>
#endif

#ifdef __cplusplus
#define JSON_INLINE inline
#else
#define JSON_INLINE inline
#endif

#define json_int_t long long
#define json_strtoint strtoll
#define JSON_INTEGER_FORMAT "lld"
#define JSON_HAVE_ATOMIC_BUILTINS 1
#define JSON_HAVE_SYNC_BUILTINS 1
#define JSON_PARSER_MAX_DEPTH 2048

#endif
