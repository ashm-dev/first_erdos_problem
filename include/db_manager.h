/**
 * db_manager.h - Менеджер базы данных SQLite
 *
 * Сохранение и загрузка результатов решения.
 */

#ifndef ERDOS_DB_MANAGER_H
#define ERDOS_DB_MANAGER_H

#include <stdbool.h>
#include <sqlite3.h>
#include <pthread.h>
#include "types.h"

// ============================================================================
// Структура менеджера БД
// ============================================================================

typedef struct {
    sqlite3 *db;
    char *db_path;
    pthread_mutex_t mutex;
    bool initialized;
} DatabaseManager;

// ============================================================================
// Функции инициализации
// ============================================================================

/**
 * Создание менеджера БД
 */
DatabaseManager* db_manager_create(const char *db_path);

/**
 * Освобождение менеджера БД
 */
void db_manager_destroy(DatabaseManager *manager);

// ============================================================================
// Функции сохранения
// ============================================================================

/**
 * Сохранение результата решения
 */
bool db_manager_save_result(DatabaseManager *manager, const SolutionResult *result);

/**
 * Сохранение всех оптимальных множеств для N
 */
bool db_manager_save_optimal_sets(DatabaseManager *manager, uint32_t n,
                                  const NumberSet *sets, size_t count);

// ============================================================================
// Функции загрузки
// ============================================================================

/**
 * Получение лучшего результата для N
 * Возвращает true если результат найден
 */
bool db_manager_get_result(DatabaseManager *manager, uint32_t n,
                           SolutionResult *result);

/**
 * Получение лучшей известной границы для N
 * Возвращает true если граница найдена
 */
bool db_manager_get_best_bound(DatabaseManager *manager, uint32_t n, value_t *bound);

/**
 * Проверка наличия оптимального решения для N
 */
bool db_manager_has_optimal_solution(DatabaseManager *manager, uint32_t n);

/**
 * Получение последнего решенного N
 * Возвращает 0 если решений нет
 */
uint32_t db_manager_get_last_n(DatabaseManager *manager);

/**
 * Получение всех оптимальных множеств для N
 * Возвращает количество множеств, sets - массив NumberSet (нужно освободить)
 */
size_t db_manager_get_optimal_sets(DatabaseManager *manager, uint32_t n,
                                   NumberSet **sets);

/**
 * Получение всех результатов
 * Возвращает количество результатов, results - массив (нужно освободить)
 */
size_t db_manager_get_all_results(DatabaseManager *manager,
                                  SolutionResult **results);

/**
 * Получение сводки по всем N
 */
typedef struct {
    uint32_t n;
    char *max_value_str;
    size_t solutions_count;
    double computation_time;
    SolutionStatus status;
} OptimalSummary;

size_t db_manager_get_all_optimal_summary(DatabaseManager *manager,
                                          OptimalSummary **summary);

/**
 * Освобождение массива сводки
 */
void db_manager_free_summary(OptimalSummary *summary, size_t count);

// ============================================================================
// Статистика
// ============================================================================

/**
 * Получение общей статистики БД
 */
typedef struct {
    size_t total_results;
    size_t optimal_results;
    uint32_t max_n_solved;
    double total_computation_time;
} DatabaseStats;

bool db_manager_get_stats(DatabaseManager *manager, DatabaseStats *stats);

/**
 * Вывод результатов для N
 */
void db_manager_print_result(DatabaseManager *manager, uint32_t n);

/**
 * Вывод всех результатов
 */
void db_manager_print_all_results(DatabaseManager *manager);

#endif // ERDOS_DB_MANAGER_H
