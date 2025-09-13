/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <lib/printf.h>

// LOG_LEVEL: Determines the minimum level of logs to be compiled.
// (e.g., LOG_LEVEL_INFO will compile INFO, WARN, and ERROR logs, but not DEBUG)
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_BUFFER_SIZE 512

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO	1
#define LOG_LEVEL_WARN	2
#define LOG_LEVEL_ERROR 3

enum LOG_MODE {
	LOG_DIRECT,   // Output logs directly to screen/serial.
	LOG_BUFFERED, // Buffer logs (e.g., for dmesg).
	LOG_KLOG,
};

// ANSI Color Codes for Readability
#define LOG_COLOR_RESET	  "\x1b[0m"
#define LOG_COLOR_CYAN	  "\x1b[1;36m"
#define LOG_COLOR_YELLOW  "\x1b[1;33m"
#define LOG_COLOR_RED	  "\x1b[1;31m"
#define LOG_COLOR_GREEN	  "\x1b[1;32m"
#define LOG_COLOR_MAGENTA "\x1b[1;35m"
// NOTE: DEBUG has no color by default to keep it visually clean.

// Optional force-redefinition toggle
#if defined(FORCE_LOG_REDEF)
#undef log_debug
#undef log_info
#undef log_warn
#undef log_error
#undef log_init
#endif

// This internal macro handles the heavy lifting of formatting the log message.
// It captures the length from snprintf and passes it to log_output to avoid a strlen call.
// It also now checks for and reports buffer truncation.
#define _LOG_IMPL(level_str, color, fmt, ...)                                                                       \
	do {                                                                                                        \
		char __log_buf[LOG_BUFFER_SIZE];                                                                    \
		int __log_len = snprintf(__log_buf,                                                                 \
					 sizeof(__log_buf),                                                         \
					 color level_str LOG_COLOR_RESET                                            \
					 " %s:%d:%s(): " fmt "\n",                                                  \
					 __FILE__,                                                                  \
					 __LINE__,                                                                  \
					 __func__ __VA_OPT__(, ) __VA_ARGS__);                                      \
                                                                                                                    \
		if (__log_len > 0) {                                                                                \
			/* Output the original message (which might be truncated). */                               \
			/* We must ensure the length passed to log_output doesn't exceed the actual buffer size. */ \
			int __len_to_write =                                                                        \
				(__log_len < (int)sizeof(__log_buf)) ?                                              \
					__log_len :                                                                 \
					((int)sizeof(__log_buf) - 1);                                               \
			log_output(__log_buf, __len_to_write);                                                      \
		}                                                                                                   \
                                                                                                                    \
		/* If snprintf's return value indicates the buffer was too small, print a warning. */               \
		if (__log_len >= (int)sizeof(__log_buf)) {                                                          \
			static const char __trunc_msg[] = LOG_COLOR_RED                                             \
				"[LOG TRUNCATED]\n" LOG_COLOR_RESET;                                                \
			log_output(                                                                                 \
				__trunc_msg,                                                                        \
				sizeof(__trunc_msg) -                                                               \
					1); /* -1 to exclude null terminator */                                     \
		}                                                                                                   \
	} while (0)

#define _LOG_UNUSED(fmt, ...)                                               \
	do {                                                                \
		(void)fmt;                                                  \
		(void)(0 __VA_OPT__(, __VA_ARGS__));                        \
		if (0) {                                                    \
			/* compile-time format checking, no runtime cost */ \
			(void)snprintf((char*)0,                            \
				       0,                                   \
				       fmt __VA_OPT__(, ) __VA_ARGS__);     \
		}                                                           \
	} while (0)

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define log_debug(fmt, ...) \
	_LOG_IMPL("[DEBUG]", "", fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define log_debug(fmt, ...) _LOG_UNUSED(fmt __VA_OPT__(, __VA_ARGS__))
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define log_info(fmt, ...) \
	_LOG_IMPL("[INFO] ", LOG_COLOR_CYAN, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define log_info(fmt, ...) _LOG_UNUSED(fmt __VA_OPT__(, __VA_ARGS__))
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define log_warn(fmt, ...) \
	_LOG_IMPL("[WARN] ", LOG_COLOR_YELLOW, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define log_warn(fmt, ...) _LOG_UNUSED(fmt __VA_OPT__(, __VA_ARGS__))
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define log_error(fmt, ...) \
	_LOG_IMPL("[ERROR]", LOG_COLOR_RED, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define log_error(fmt, ...) _LOG_UNUSED(fmt __VA_OPT__(, __VA_ARGS__))
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define log_init(fmt, ...) \
	_LOG_IMPL("[INIT] ", LOG_COLOR_GREEN, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define log_init(fmt, ...) _LOG_UNUSED(fmt __VA_OPT__(, __VA_ARGS__))
#endif

/**
 * @brief Sets the logging mode (direct or buffered).
 * @param mode The LOG_MODE to set.
 */
void set_log_mode(enum LOG_MODE mode);

/**
 * @brief The backend function that handles the actual output of the formatted log message.
 * @param msg The pre-formatted message string to output.
 * @param len The length of the message string.
 */
void log_output(const char* msg, int len);
