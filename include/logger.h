/**
 * logger.h - Система логирования для Erdos Solver
 *
 * Логирование на русском языке с поддержкой различных уровней.
 */

#ifndef ERDOS_LOGGER_H
#define ERDOS_LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <gmp.h>
#include "types.h"

// ============================================================================
// Глобальные переменные логгера
// ============================================================================

extern LogLevel g_log_level;
extern FILE *g_log_file;
extern pthread_mutex_t g_log_mutex;

// ============================================================================
// Функции инициализации
// ============================================================================

/**
 * Инициализация логгера
 */
void logger_init(LogLevel level, const char *log_file_path);

/**
 * Завершение работы логгера
 */
void logger_cleanup(void);

/**
 * Установка уровня логирования
 */
void logger_set_level(LogLevel level);

// ============================================================================
// Основные функции логирования
// ============================================================================

/**
 * Базовая функция логирования
 */
__attribute__((format(printf, 2, 3)))
void log_message(LogLevel level, const char *format, ...);

/**
 * Логирование с GMP числом
 */
void log_message_mpz(LogLevel level, const char *format, const mpz_t value);

// ============================================================================
// Специализированные функции логирования
// ============================================================================

/**
 * Логирование начала поиска
 */
void log_start(uint32_t n, const mpz_t initial_bound);

/**
 * Логирование прогресса поиска
 */
void log_progress(uint32_t n, uint64_t nodes, double elapsed_sec,
                  uint32_t depth, const mpz_t best_max);

/**
 * Логирование найденного решения
 */
void log_solution_found(uint32_t n, const mpz_t max_value, const MpzSet *solution);

/**
 * Логирование завершения поиска
 */
void log_complete(uint32_t n, SolutionStatus status, double total_time,
                  uint64_t total_nodes, const mpz_t max_value);

/**
 * Логирование ошибки
 */
__attribute__((format(printf, 1, 2)))
void log_error(const char *format, ...);

/**
 * Логирование предупреждения
 */
__attribute__((format(printf, 1, 2)))
void log_warning(const char *format, ...);

/**
 * Логирование отладочной информации
 */
__attribute__((format(printf, 1, 2)))
void log_debug(const char *format, ...);

// ============================================================================
// Макросы для удобства
// ============================================================================

#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARNING(...) log_message(LOG_LEVEL_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif // ERDOS_LOGGER_H
