#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"

log_level_t current_log_level = LOG_LEVEL_INFO;

void _log_message(const char* message, log_level_t level, ...) {
    if (level >= current_log_level) {
        // Get timestamp
        TickType_t tick_count = xTaskGetTickCount();
        uint32_t time_ms = tick_count * portTICK_PERIOD_MS;

        // Convert milliseconds to seconds and microseconds
        uint32_t seconds = time_ms / 1000;
        uint32_t microseconds = (time_ms % 1000) * 1000;

        // Select color based on log level
        const char* color;
        switch (level) {
            case LOG_LEVEL_DEBUG: color = COLOR_GREEN; break;
            case LOG_LEVEL_INFO:  color = COLOR_RESET; break;
            case LOG_LEVEL_WARN:  color = COLOR_YELLOW; break;
            case LOG_LEVEL_ERROR: color = COLOR_RED; break;
            default: color = COLOR_RESET; break;
        }

        // Start processing variadic arguments
        va_list args;
        va_start(args, level);

        // Print timestamp directly
        printf("%s[%6u.%06u] %s", COLOR_BLUE, seconds, microseconds, color);
        vprintf(message, args);
        printf("%s", COLOR_RESET);

        // Clean up variadic argument list
        va_end(args);
    }
}

void set_log_level(log_level_t level) {
    current_log_level = level;
}
