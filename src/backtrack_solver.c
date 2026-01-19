/**
 * backtrack_solver.c - Высокопроизводительная реализация алгоритма backtracking
 *
 * Использует нативную арифметику uint64_t.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/backtrack_solver.h"
#include "../include/logger.h"

// ============================================================================
// Вспомогательные функции
// ============================================================================

value_t compute_initial_bound(uint32_t n) {
    // Верхняя граница: 2^(n-1) + 1
    if (n == 0) return 1;
    return (1ULL << (n - 1)) + 1;
}

bool is_valid_b_sequence(const NumberSet *set) {
    if (set->size == 0) return true;

    // Создаем временный менеджер для проверки
    SubsetSumManager *manager = subset_sum_manager_create(MANAGER_TYPE_FAST);

    for (size_t i = 0; i < set->size; i++) {
        if (!subset_sum_manager_add_element(manager, set->elements[i])) {
            subset_sum_manager_destroy(manager);
            return false;
        }
    }

    subset_sum_manager_destroy(manager);
    return true;
}

// ============================================================================
// Создание и уничтожение
// ============================================================================

BacktrackSolver* backtrack_solver_create(const SolverConfig *config) {
    BacktrackSolver *solver = malloc(sizeof(BacktrackSolver));

    // Копируем конфигурацию
    solver->config = *config;

    // Определяем тип менеджера: быстрый для N < 25, итеративный для N >= 25
    ManagerType manager_type = config->manager_type;
    if (config->n >= 25 && manager_type == MANAGER_TYPE_FAST) {
        LOG_WARNING("N=%u слишком велико для быстрого режима, переключаемся на итеративный",
                    config->n);
        manager_type = MANAGER_TYPE_ITERATIVE;
    }

    solver->manager = subset_sum_manager_create(manager_type);

    // Инициализируем лучшее решение
    solver->best_max = 0;
    number_set_init(&solver->best_solution, config->n);
    solver->has_solution = false;

    // Инициализируем массив всех оптимальных решений
    solver->all_optimal_solutions = NULL;
    solver->optimal_count = 0;
    solver->optimal_capacity = 0;

    // Инициализируем статистику
    memset(&solver->stats, 0, sizeof(SearchStats));

    // Callbacks
    solver->solution_callback = NULL;
    solver->progress_callback = NULL;
    solver->callback_user_data = NULL;

    return solver;
}

void backtrack_solver_destroy(BacktrackSolver *solver) {
    if (!solver) return;

    subset_sum_manager_destroy(solver->manager);
    number_set_clear(&solver->best_solution);

    // Освобождаем все оптимальные решения
    if (solver->all_optimal_solutions) {
        for (size_t i = 0; i < solver->optimal_count; i++) {
            number_set_clear(&solver->all_optimal_solutions[i]);
        }
        free(solver->all_optimal_solutions);
    }

    free(solver);
}

void backtrack_solver_set_solution_callback(BacktrackSolver *solver,
                                            SolutionCallback callback,
                                            void *user_data) {
    solver->solution_callback = callback;
    solver->callback_user_data = user_data;
}

void backtrack_solver_set_progress_callback(BacktrackSolver *solver,
                                            ProgressCallback callback,
                                            void *user_data) {
    solver->progress_callback = callback;
    solver->callback_user_data = user_data;
}

// ============================================================================
// Основной алгоритм backtracking
// ============================================================================

/**
 * Сохранение текущего решения как нового лучшего
 */
static void save_best_solution(BacktrackSolver *solver) {
    // Копируем текущие элементы как лучшее решение
    subset_sum_manager_get_elements(solver->manager, &solver->best_solution);

    // Находим максимальный элемент
    solver->best_max = 0;
    for (size_t i = 0; i < solver->best_solution.size; i++) {
        if (solver->best_solution.elements[i] > solver->best_max) {
            solver->best_max = solver->best_solution.elements[i];
        }
    }

    solver->has_solution = true;
    solver->stats.best_max = solver->best_max;
    solver->stats.solutions_found++;

    // Вызываем callback
    if (solver->solution_callback) {
        solver->solution_callback(solver->config.n, solver->best_max,
                                  &solver->best_solution, solver->callback_user_data);
    }

    log_solution_found(solver->config.n, solver->best_max, &solver->best_solution);
}

/**
 * Добавление решения в список оптимальных
 */
static void add_optimal_solution(BacktrackSolver *solver) {
    if (solver->optimal_count >= solver->optimal_capacity) {
        size_t new_capacity = solver->optimal_capacity == 0 ? 16 : solver->optimal_capacity * 2;
        solver->all_optimal_solutions = realloc(solver->all_optimal_solutions,
                                                new_capacity * sizeof(NumberSet));
        for (size_t i = solver->optimal_capacity; i < new_capacity; i++) {
            number_set_init(&solver->all_optimal_solutions[i], solver->config.n);
        }
        solver->optimal_capacity = new_capacity;
    }

    subset_sum_manager_get_elements(solver->manager,
                                    &solver->all_optimal_solutions[solver->optimal_count]);
    solver->optimal_count++;
}

/**
 * Проверка и логирование прогресса
 */
static void check_progress(BacktrackSolver *solver) {
    time_t now = time(NULL);
    if (now - solver->stats.last_log_time >= solver->config.log_interval_sec) {
        solver->stats.last_log_time = now;

        double elapsed = difftime(now, solver->stats.start_time);
        log_progress(solver->config.n, solver->stats.nodes_explored, elapsed,
                     solver->stats.current_depth, solver->stats.best_max);

        if (solver->progress_callback) {
            solver->progress_callback(&solver->stats, solver->callback_user_data);
        }
    }
}

/**
 * Рекурсивная функция backtracking
 *
 * @param solver     Контекст решателя
 * @param depth      Текущая глубина (количество уже добавленных элементов)
 * @param min_next   Минимальное значение следующего элемента
 */
static void backtrack(BacktrackSolver *solver, uint32_t depth, value_t min_next) {
    // Проверка флага остановки
    if (solver->config.stop_flag && *solver->config.stop_flag) {
        return;
    }

    // Увеличиваем счетчик узлов
    solver->stats.nodes_explored++;
    solver->stats.current_depth = depth;

    // Периодическая проверка прогресса
    uint64_t check_mask = solver->stats.nodes_explored > 100000 ? 0xFFFF : 0x3FF;
    if ((solver->stats.nodes_explored & check_mask) == 0) {
        check_progress(solver);
    }

    // Базовый случай: найдено полное множество
    if (depth == solver->config.n) {
        // Находим максимум текущего решения
        value_t current_max = 0;
        size_t size = subset_sum_manager_size(solver->manager);
        for (size_t i = 0; i < size; i++) {
            value_t elem = subset_sum_manager_get_element(solver->manager, i);
            if (elem > current_max) {
                current_max = elem;
            }
        }

        if (!solver->config.find_all_optimal) {
            // Обычный режим - только первое лучшее решение
            if (current_max < solver->best_max) {
                save_best_solution(solver);
            }
        } else {
            // Режим поиска всех оптимальных
            if (!solver->has_solution || current_max < solver->best_max) {
                // Новый лучший максимум - очищаем старые решения
                solver->optimal_count = 0;
                save_best_solution(solver);
                add_optimal_solution(solver);
            } else if (current_max == solver->best_max) {
                // Равный максимум - добавляем к списку
                add_optimal_solution(solver);
                solver->stats.solutions_found++;
                if (solver->optimal_count <= 10) {
                    LOG_INFO("Found another optimal: N=%u, total=%zu",
                             solver->config.n, solver->optimal_count);
                }
            }
        }

        return;
    }

    // Отсечение 1: минимально возможный максимум
    uint32_t remaining = solver->config.n - depth - 1;
    value_t min_possible = min_next + remaining;

    if (solver->has_solution && min_possible >= solver->best_max) {
        return;  // Отсечение: не можем улучшить текущий лучший результат
    }

    // Перебор кандидатов
    value_t candidate = min_next;

    // Цикл пока кандидат меньше верхней границы
    for (;;) {
        // Проверка флага остановки
        if (solver->config.stop_flag && *solver->config.stop_flag) {
            return;
        }

        // Динамическая проверка верхней границы
        if (solver->has_solution) {
            if (candidate >= solver->best_max) {
                break;
            }
        } else {
            if (candidate >= solver->config.initial_bound) {
                break;
            }
        }

        // Отсечение 2: candidate + remaining >= best_max
        if (solver->has_solution && (candidate + remaining) >= solver->best_max) {
            break;  // Все дальнейшие кандидаты еще хуже
        }

        // Попытка добавить кандидата
        if (subset_sum_manager_add_element(solver->manager, candidate)) {
            // Успешно добавлен - рекурсивный вызов
            backtrack(solver, depth + 1, candidate + 1);

            // Откат
            subset_sum_manager_remove_last(solver->manager);

            // В режиме first_only останавливаемся после первого решения
            if (solver->config.first_only && solver->has_solution) {
                return;
            }
        }

        candidate++;
    }
}

// ============================================================================
// Публичные функции решения
// ============================================================================

void backtrack_solver_solve(BacktrackSolver *solver, SolutionResult *result) {
    // Инициализация
    solver->has_solution = false;
    solver->stats.nodes_explored = 0;
    solver->stats.solutions_found = 0;
    solver->stats.start_time = time(NULL);
    solver->stats.last_log_time = solver->stats.start_time;
    solver->stats.current_depth = 0;

    // Устанавливаем начальную границу
    if (solver->config.initial_bound == 0) {
        solver->config.initial_bound = compute_initial_bound(solver->config.n);
    }
    // best_max = initial_bound (как в Python)
    solver->best_max = solver->config.initial_bound;
    solver->stats.best_max = solver->config.initial_bound;

    log_start(solver->config.n, solver->config.initial_bound);

    double start_time = get_time_sec();

    // Особый случай для N=1
    if (solver->config.n == 1) {
        solver->best_max = 1;
        solver->best_solution.size = 1;
        solver->best_solution.elements[0] = 1;
        solver->has_solution = true;
        log_solution_found(solver->config.n, solver->best_max, &solver->best_solution);
    } else {
        // Запуск backtracking
        backtrack(solver, 0, 1);
    }

    double elapsed = get_time_sec() - start_time;

    // Заполняем результат
    result->n = solver->config.n;
    if (solver->has_solution) {
        result->max_value = solver->best_max;
        number_set_copy(&result->solution_set, &solver->best_solution);
        result->status = SOLUTION_STATUS_OPTIMAL;
    } else {
        result->max_value = 0;
        result->solution_set.size = 0;
        if (solver->config.stop_flag && *solver->config.stop_flag) {
            result->status = SOLUTION_STATUS_INTERRUPTED;
        } else {
            result->status = SOLUTION_STATUS_NO_SOLUTION;
        }
    }
    result->computation_time = elapsed;
    result->nodes_explored = solver->stats.nodes_explored;
    result->timestamp = time(NULL);

    log_complete(solver->config.n, result->status, elapsed, solver->stats.nodes_explored, solver->best_max);
}

void backtrack_solver_solve_all(BacktrackSolver *solver, SolutionResult *result) {
    // Устанавливаем режим поиска всех оптимальных
    solver->config.find_all_optimal = true;
    solver->optimal_count = 0;

    // Запускаем стандартный solve
    backtrack_solver_solve(solver, result);

    LOG_INFO("Найдено %zu оптимальных решений для N=%u",
             solver->optimal_count, solver->config.n);
}

size_t backtrack_solver_get_optimal_solutions(const BacktrackSolver *solver,
                                              NumberSet **solutions) {
    *solutions = solver->all_optimal_solutions;
    return solver->optimal_count;
}

void backtrack_solver_get_stats(const BacktrackSolver *solver, SearchStats *stats) {
    *stats = solver->stats;
}
