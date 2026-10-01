/**
 * @file macro.h
 * @brief 宏定义工具
 * @author Christins
 * @date 2024-11-09
 */
#pragma once

#include <cassert>
#include <cstring>

#include "../log/log.h"

#if defined __GNUC__ || defined __llvm__
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

#define ASSERT(x)                                                     \
    if (UNLIKELY(!(x))) {                                             \
        ERROR(LOG_ROOT()) << "ASSERTION: " #x << "\n"                 \
                          << "bracktrace: \n"                         \
                          << chen::BacktraceToString(100, 2, "    "); \
        assert(x);                                                    \
    }

#define ASSERT_MSG(x, msg)                                            \
    if (UNLIKELY(!(x))) {                                             \
        ERROR(LOG_ROOT()) << "ASSERTION: " #x << "\n"                 \
                          << msg << "\n"                              \
                          << "bracktrace: \n"                         \
                          << chen::BacktraceToString(100, 2, "    "); \
        assert(x);                                                    \
    }

#define ASSERT_RET(x, ...)                     \
    if (UNLIKELY(!(x))) {                      \
        ERROR(LOG_ROOT()) << "ASSERTION: " #x; \
        return __VA_ARGS__;                    \
    }

// 便捷颜色宏 (完整ANSI转义序列)
#define COLOR_RESET        "\033[0m"
#define COLOR_RED          "\033[31m"
#define COLOR_GREEN        "\033[32m"
#define COLOR_YELLOW       "\033[33m"
#define COLOR_BLUE         "\033[34m"
#define COLOR_MAGENTA      "\033[35m"
#define COLOR_CYAN         "\033[36m"
#define COLOR_WHITE        "\033[37m"
#define COLOR_BOLD_RED     "\033[1;31m"
#define COLOR_BOLD_GREEN   "\033[1;32m"
#define COLOR_BOLD_YELLOW  "\033[1;33m"
#define COLOR_BOLD_BLUE    "\033[1;34m"
#define COLOR_BOLD_MAGENTA "\033[1;35m"
#define COLOR_BOLD_CYAN    "\033[1;36m"
#define COLOR_BG_RED       "\033[41m"
#define COLOR_BG_GREEN     "\033[42m"
#define COLOR_BG_YELLOW    "\033[43m"
#define COLOR_BG_BLUE      "\033[44m"
#define COLOR_BG_MAGENTA   "\033[45m"
#define COLOR_BG_CYAN      "\033[46m"
#define COLOR_BG_WHITE     "\033[47m"
