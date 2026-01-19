/**
 * backtrack_solver.h - Алгоритм поиска методом backtracking
 *
 * Реализация поиска множеств с различными суммами подмножеств.
 */

#ifndef ERDOS_BACKTRACK_SOLVER_H
#define ERDOS_BACKTRACK_SOLVER_H

#include <stdbool.h>
#include "types.h"
#include "subset_sum_manager.h"

// ============================================================================
// Callback типы
// ============================================================================

/**
 * Callback для найденного решения
 * Вызывается каждый раз при нахождении нового лучшего решения
 */
typedef void (*SolutionCallback)(uint32_t n, value_t max_value,
                                  const NumberSet *solution, void *user_data);

/**
 * Callback для прогресса
 * Вызывается периодически для отчета о прогрессе
 */
typedef void (*ProgressCallback)(const SearchStats *stats, void *user_data);

// ============================================================================
// Структура решателя
// ============================================================================

/**
 * Контекст backtrack решателя
 */
typedef struct {
    // Конфигурация
    SolverConfig config;

    // Менеджер сумм
    SubsetSumManager *manager;

    // Текущее лучшее решение
    value_t best_max;
    NumberSet best_solution;
    bool has_solution;

    // Все оптимальные решения (если find_all_optimal = true)
    NumberSet *all_optimal_solutions;
    size_t optimal_count;
    size_t optimal_capacity;

    // Статистика
    SearchStats stats;

    // Callbacks
    SolutionCallback solution_callback;
    ProgressCallback progress_callback;
    void *callback_user_data;
} BacktrackSolver;

// ============================================================================
// Функции решателя
// ============================================================================

/**
 * Создание решателя
 */
BacktrackSolver* backtrack_solver_create(const SolverConfig *config);

/**
 * Освобождение решателя
 */
void backtrack_solver_destroy(BacktrackSolver *solver);

/**
 * Установка callback для решений
 */
void backtrack_solver_set_solution_callback(BacktrackSolver *solver,
                                            SolutionCallback callback,
                                            void *user_data);

/**
 * Установка callback для прогресса
 */
void backtrack_solver_set_progress_callback(BacktrackSolver *solver,
                                            ProgressCallback callback,
                                            void *user_data);

/**
 * Решение задачи - поиск первого оптимального решения
 * Возвращает результат в структуру result
 */
void backtrack_solver_solve(BacktrackSolver *solver, SolutionResult *result);

/**
 * Решение задачи - поиск всех оптимальных решений
 * Возвращает результат в структуру result,
 * все оптимальные множества доступны через backtrack_solver_get_optimal_solutions
 */
void backtrack_solver_solve_all(BacktrackSolver *solver, SolutionResult *result);

/**
 * Получение всех оптимальных решений
 * Возвращает количество решений, solutions - массив NumberSet
 */
size_t backtrack_solver_get_optimal_solutions(const BacktrackSolver *solver,
                                              NumberSet **solutions);

/**
 * Получение статистики поиска
 */
void backtrack_solver_get_stats(const BacktrackSolver *solver, SearchStats *stats);

// ============================================================================
// Вспомогательные функции
// ============================================================================

/**
 * Вычисление начальной верхней границы для N
 * По умолчанию: 2^(n-1) + 1
 */
value_t compute_initial_bound(uint32_t n);

/**
 * Проверка, является ли множество B-последовательностью
 * (все суммы подмножеств различны)
 */
bool is_valid_b_sequence(const NumberSet *set);

#endif // ERDOS_BACKTRACK_SOLVER_H
