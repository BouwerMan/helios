/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <printf.h>

enum LOG_MODE {
	LOG_DIRECT,
	LOG_BUFFERED,
};

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO	1
#define LOG_LEVEL_WARN	2
#define LOG_LEVEL_ERROR 3

#define LOG_BUFFER_SIZE 512

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// Optional force-redefinition toggle
#if defined(FORCE_LOG_REDEF)
#undef log_debug
#undef log_info
#undef log_warn
#undef log_error
#undef log_debug_long
#endif
// TODO: Make screen accept ANSI color codes

// Define or redefine log_debug
#if !defined(log_debug) || defined(FORCE_LOG_REDEF)
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define log_debug(fmt, ...)                                                                                           \
	do {                                                                                                          \
		char __log_buf[LOG_BUFFER_SIZE];                                                                      \
		snprintf(__log_buf, sizeof(__log_buf), "[DEBUG] %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, \
			 ##__VA_ARGS__);                                                                              \
		log_output(__log_buf);                                                                                \
	} while (0)
#else
#define log_debug(fmt, ...) ((void)0)
#endif
#endif

// Define or redefine log_info
#if !defined(log_info) || defined(FORCE_LOG_REDEF)
#if LOG_LEVEL <= LOG_LEVEL_INFO
#define log_info(fmt, ...)                                                                                         \
	do {                                                                                                       \
		char __log_buf[LOG_BUFFER_SIZE];                                                                   \
		snprintf(__log_buf, sizeof(__log_buf), "\x1b[1;36m[INFO]\x1b[0m  %s:%d:%s(): " fmt "\n", __FILE__, \
			 __LINE__, __func__, ##__VA_ARGS__);                                                       \
		log_output(__log_buf);                                                                             \
	} while (0)
#else
#define log_info(fmt, ...) ((void)0)
#endif
#endif

// Define or redefine log_warn
#if !defined(log_warn) || defined(FORCE_LOG_REDEF)
#if LOG_LEVEL <= LOG_LEVEL_WARN
#define log_warn(fmt, ...)                                                                                         \
	do {                                                                                                       \
		char __log_buf[LOG_BUFFER_SIZE];                                                                   \
		snprintf(__log_buf, sizeof(__log_buf), "\x1b[1;33m[WARN]\x1b[0m  %s:%d:%s(): " fmt "\n", __FILE__, \
			 __LINE__, __func__, ##__VA_ARGS__);                                                       \
		log_output(__log_buf);                                                                             \
	} while (0)
#else
#define log_warn(fmt, ...) ((void)0)
#endif
#endif

// Define or redefine log_error
#if !defined(log_error) || defined(FORCE_LOG_REDEF)
#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define log_error(fmt, ...)                                                                                          \
	do {                                                                                                         \
		char __log_buf[LOG_BUFFER_SIZE];                                                                     \
		snprintf(__log_buf, sizeof(__log_buf), "\x1b[1;31m[ERROR]\x1b[1;0m %s:%d:%s(): " fmt "\n", __FILE__, \
			 __LINE__, __func__, ##__VA_ARGS__);                                                         \
		log_output(__log_buf);                                                                               \
	} while (0)
#else
#define log_error(fmt, ...) ((void)0)
#endif
#endif

// Define or redefine log_debug_long
#if !defined(log_debug_long) || defined(FORCE_LOG_REDEF)
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define log_debug_long(msg) log_long_message("DEBUG", __FILE__, __LINE__, __func__, msg)
#else
#define log_debug_long(msg) ((void)0)
#endif
#endif

// Function declarations

void log_putchar(const char c);
void set_log_mode(enum LOG_MODE mode);
void log_output(const char* msg);
