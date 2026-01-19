/**
 * logger.c - Реализация системы логирования
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <locale.h>
#include <inttypes.h>
#include "../include/logger.h"

// ============================================================================
// Глобальные переменные
// ============================================================================

LogLevel g_log_level = LOG_LEVEL_INFO;
FILE *g_log_file = NULL;
pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// Вспомогательные функции
// ============================================================================

static const char* level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_WARNING: return "WARNING";
        case LOG_LEVEL_ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

static void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%d.%m.%Y %H:%M:%S", tm_info);
}

static void format_number_with_underscores(char *buffer, size_t size, uint64_t value) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%" PRIu64, value);

    size_t len = strlen(temp);
    size_t out_idx = 0;

    for (size_t i = 0; i < len && out_idx < size - 1; i++) {
        size_t pos_from_end = len - 1 - i;
        if (i > 0 && pos_from_end % 3 == 2) {
            buffer[out_idx++] = '_';
        }
        buffer[out_idx++] = temp[i];
    }
    buffer[out_idx] = '\0';
}

// ============================================================================
// Функции инициализации
// ============================================================================

void logger_init(LogLevel level, const char *log_file_path) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    g_log_level = level;

    if (log_file_path) {
        g_log_file = fopen(log_file_path, "a");
        if (!g_log_file) {
            fprintf(stderr, "Ошибка: не удалось открыть файл лога %s\n", log_file_path);
        }
    }
}

void logger_cleanup(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_set_level(LogLevel level) {
    g_log_level = level;
}

// ============================================================================
// Основные функции логирования
// ============================================================================

void log_message(LogLevel level, const char *format, ...) {
    if (level < g_log_level) {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    FILE *out = (level >= LOG_LEVEL_WARNING) ? stderr : stdout;

    // Формат: ДД.ММ.ГГГГ ЧЧ:ММ:СС [LEVEL] message
    fprintf(out, "%s [%s] ", timestamp, level_to_string(level));

    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);

    // Дублируем в файл, если открыт
    if (g_log_file) {
        fprintf(g_log_file, "%s [%s] ", timestamp, level_to_string(level));
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

// ============================================================================
// Специализированные функции логирования
// ============================================================================

void log_start(uint32_t n, value_t initial_bound) {
    log_message(LOG_LEVEL_INFO, "Starting N=%u, upper_bound=%" PRIu64, n, initial_bound);
}

void log_progress(uint32_t n, uint64_t nodes, double elapsed_sec,
                  uint32_t depth, value_t best_max) {
    char nodes_str[64];
    format_number_with_underscores(nodes_str, sizeof(nodes_str), nodes);

    log_message(LOG_LEVEL_INFO,
                "N=%u: nodes=%s, time=%.1fs, depth=%u, best=%" PRIu64,
                n, nodes_str, elapsed_sec, depth, best_max);
}

void log_solution_found(uint32_t n, value_t max_value, const NumberSet *solution) {
    (void)solution;  // Не выводим множество, как в Python
    log_message(LOG_LEVEL_INFO, "Found better: N=%u, max=%" PRIu64, n, max_value);
}

void log_complete(uint32_t n, SolutionStatus status, double total_time,
                  uint64_t total_nodes, value_t max_value) {
    char nodes_str[64];
    format_number_with_underscores(nodes_str, sizeof(nodes_str), total_nodes);

    if (status == SOLUTION_STATUS_OPTIMAL) {
        log_message(LOG_LEVEL_INFO,
                    "Finished N=%u, max=%" PRIu64 ", nodes=%s, time=%.2fs",
                    n, max_value, nodes_str, total_time);
    } else if (status == SOLUTION_STATUS_INTERRUPTED) {
        log_message(LOG_LEVEL_INFO,
                    "Interrupted N=%u, nodes=%s, time=%.2fs",
                    n, nodes_str, total_time);
    } else {
        log_message(LOG_LEVEL_INFO,
                    "No solution for N=%u, nodes=%s, time=%.2fs",
                    n, nodes_str, total_time);
    }
}

void log_error(const char *format, ...) {
    pthread_mutex_lock(&g_log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    fprintf(stderr, "%s [ERROR] ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);

    if (g_log_file) {
        fprintf(g_log_file, "%s [ERROR] ", timestamp);
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

void log_warning(const char *format, ...) {
    if (LOG_LEVEL_WARNING < g_log_level) {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    fprintf(stderr, "%s [WARNING] ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);

    if (g_log_file) {
        fprintf(g_log_file, "%s [WARNING] ", timestamp);
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

void log_debug(const char *format, ...) {
    if (LOG_LEVEL_DEBUG < g_log_level) {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("%s [DEBUG] ", timestamp);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    if (g_log_file) {
        fprintf(g_log_file, "%s [DEBUG] ", timestamp);
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}
