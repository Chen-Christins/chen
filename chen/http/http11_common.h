/**
 * @file http11_common.h
 * @brief HTTP/1.1 Ragel 解析通用定义
 */
#pragma once

#include <sys/types.h>

#ifndef _http11_common_h
#define _http11_common_h


typedef void (*element_cb)(void *data, const char *at, size_t length);
typedef void (*field_cb)(void *data, const char *field, size_t flen, const char *value, size_t vlen);

#endif
