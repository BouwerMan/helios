#pragma once
#include <printf.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO	1
#define LOG_LEVEL_WARN	2
#define LOG_LEVEL_ERROR 3

#define LOG_BUFFER_SIZE 512

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define log_debug(fmt, ...)                                                   \
	do {                                                                  \
		char __log_buf[LOG_BUFFER_SIZE];                              \
		snprintf(__log_buf, sizeof(__log_buf),                        \
			 "[DEBUG] %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, \
			 __func__, ##__VA_ARGS__);                            \
		log_output(__log_buf);                                        \
	} while (0)
#else
#define log_debug(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define log_info(fmt, ...)                                                    \
	do {                                                                  \
		char __log_buf[LOG_BUFFER_SIZE];                              \
		snprintf(__log_buf, sizeof(__log_buf),                        \
			 "[INFO]  %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, \
			 __func__, ##__VA_ARGS__);                            \
		log_output(__log_buf);                                        \
	} while (0)
#else
#define log_info(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define log_warn(fmt, ...)                                                    \
	do {                                                                  \
		char __log_buf[LOG_BUFFER_SIZE];                              \
		snprintf(__log_buf, sizeof(__log_buf),                        \
			 "[WARN]  %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, \
			 __func__, ##__VA_ARGS__);                            \
		log_output(__log_buf);                                        \
	} while (0)
#else
#define log_warn(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define log_error(fmt, ...)                                                   \
	do {                                                                  \
		char __log_buf[LOG_BUFFER_SIZE];                              \
		snprintf(__log_buf, sizeof(__log_buf),                        \
			 "[ERROR] %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, \
			 __func__, ##__VA_ARGS__);                            \
		log_output(__log_buf);                                        \
	} while (0)
#else
#define log_error(fmt, ...) ((void)0)
#endif

void log_output(const char* msg);
