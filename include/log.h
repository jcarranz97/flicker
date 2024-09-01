#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdarg.h>

// Define colors
#define COLOR_RESET "\033[0m"
#define COLOR_BLUE "\033[34m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED "\033[31m"

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

// Define a macro for _log_message with the default level set to INFO
#define log_debug(message, ...) _log_message(message, LOG_LEVEL_DEBUG, ##__VA_ARGS__)
#define log_info(message, ...) _log_message(message, LOG_LEVEL_INFO, ##__VA_ARGS__)
#define log_warn(message, ...) _log_message(message, LOG_LEVEL_WARN, ##__VA_ARGS__)
#define log_error(message, ...) _log_message(message, LOG_LEVEL_ERROR, ##__VA_ARGS__)

void _log_message(const char* format, log_level_t level, ...);
void set_log_level(log_level_t level);

extern log_level_t current_log_level;

#endif // LOGGING_H
