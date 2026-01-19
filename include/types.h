/**
 * types.h - Типы данных и константы для Erdos Solver
 *
 * Высокопроизводительная реализация на C23 с нативной арифметикой uint64_t.
 */

#ifndef ERDOS_TYPES_H
#define ERDOS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

// ============================================================================
// Константы
// ============================================================================

#define ERDOS_MAX_SET_SIZE 64
#define ERDOS_DEFAULT_DB_PATH "erdos_results.db"
#define ERDOS_LOG_INTERVAL_SEC 60

// ============================================================================
// Основной числовой тип
// ============================================================================

typedef uint64_t value_t;

#define VALUE_MAX UINT64_MAX
#define VALUE_FMT PRIu64

// ============================================================================
// Перечисления
// ============================================================================

/**
 * Статус решения
 */
typedef enum {
    SOLUTION_STATUS_OPTIMAL,      // Найдено оптимальное решение
    SOLUTION_STATUS_FEASIBLE,     // Найдено допустимое решение
    SOLUTION_STATUS_NO_SOLUTION,  // Решение не найдено
    SOLUTION_STATUS_TIMEOUT,      // Превышено время
    SOLUTION_STATUS_INTERRUPTED   // Прервано пользователем
} SolutionStatus;

/**
 * Тип менеджера сумм
 */
typedef enum {
    MANAGER_TYPE_FAST,       // Быстрый (O(2^N) память)
    MANAGER_TYPE_ITERATIVE   // Итеративный (O(N) память)
} ManagerType;

/**
 * Уровень логирования
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

// ============================================================================
// Структуры данных
// ============================================================================

/**
 * Множество чисел (элементы решения)
 */
typedef struct {
    value_t *elements;    // Массив элементов
    size_t size;          // Текущее количество элементов
    size_t capacity;      // Выделенная емкость
} NumberSet;

/**
 * Результат решения
 */
typedef struct {
    uint32_t n;                   // Размер множества
    value_t max_value;            // Максимальный элемент
    NumberSet solution_set;       // Найденное множество
    double computation_time;      // Время вычисления в секундах
    SolutionStatus status;        // Статус решения
    uint64_t nodes_explored;      // Количество исследованных узлов
    time_t timestamp;             // Время завершения
} SolutionResult;

/**
 * Конфигурация решателя
 */
typedef struct {
    uint32_t n;                    // Размер искомого множества
    value_t initial_bound;         // Начальная верхняя граница (0 = авто)
    bool find_all_optimal;         // Искать все оптимальные решения
    bool first_only;               // Остановиться на первом решении
    ManagerType manager_type;      // Тип менеджера сумм
    uint32_t log_interval_sec;     // Интервал логирования
    volatile bool *stop_flag;      // Флаг остановки (для graceful shutdown)
} SolverConfig;

/**
 * Статистика поиска
 */
typedef struct {
    uint64_t nodes_explored;       // Всего узлов
    uint32_t current_depth;        // Текущая глубина
    value_t best_max;              // Лучший найденный максимум
    uint32_t solutions_found;      // Количество найденных решений
    time_t start_time;             // Время начала
    time_t last_log_time;          // Время последнего лога
} SearchStats;

// ============================================================================
// Функции работы с NumberSet
// ============================================================================

/**
 * Инициализация множества
 */
static inline void number_set_init(NumberSet *set, size_t initial_capacity) {
    set->capacity = initial_capacity > 0 ? initial_capacity : 16;
    set->size = 0;
    set->elements = malloc(set->capacity * sizeof(value_t));
}

/**
 * Освобождение памяти множества
 */
static inline void number_set_clear(NumberSet *set) {
    if (set->elements) {
        free(set->elements);
        set->elements = NULL;
    }
    set->size = 0;
    set->capacity = 0;
}

/**
 * Добавление элемента в множество
 */
static inline void number_set_push(NumberSet *set, value_t value) {
    if (set->size >= set->capacity) {
        set->capacity *= 2;
        set->elements = realloc(set->elements, set->capacity * sizeof(value_t));
    }
    set->elements[set->size++] = value;
}

/**
 * Удаление последнего элемента
 */
static inline void number_set_pop(NumberSet *set) {
    if (set->size > 0) {
        set->size--;
    }
}

/**
 * Копирование множества
 */
static inline void number_set_copy(NumberSet *dest, const NumberSet *src) {
    number_set_clear(dest);
    number_set_init(dest, src->capacity);
    dest->size = src->size;
    memcpy(dest->elements, src->elements, src->size * sizeof(value_t));
}

/**
 * Получение строкового представления множества
 * Возвращает динамически выделенную строку (нужно освободить)
 */
static inline char* number_set_to_string(const NumberSet *set) {
    if (set->size == 0) {
        char *result = malloc(3);
        strcpy(result, "{}");
        return result;
    }

    // Оценка размера буфера (max 20 цифр на число + ", ")
    size_t buf_size = 3 + set->size * 22;
    char *result = malloc(buf_size);
    char *ptr = result;
    *ptr++ = '{';

    for (size_t i = 0; i < set->size; i++) {
        if (i > 0) {
            *ptr++ = ',';
            *ptr++ = ' ';
        }
        ptr += sprintf(ptr, "%" VALUE_FMT, set->elements[i]);
    }

    *ptr++ = '}';
    *ptr = '\0';

    return result;
}

// ============================================================================
// Функции работы с SolutionResult
// ============================================================================

/**
 * Инициализация результата
 */
static inline void solution_result_init(SolutionResult *result) {
    result->n = 0;
    result->max_value = 0;
    number_set_init(&result->solution_set, 16);
    result->computation_time = 0.0;
    result->status = SOLUTION_STATUS_NO_SOLUTION;
    result->nodes_explored = 0;
    result->timestamp = 0;
}

/**
 * Освобождение памяти результата
 */
static inline void solution_result_clear(SolutionResult *result) {
    number_set_clear(&result->solution_set);
}

// ============================================================================
// Вспомогательные функции
// ============================================================================

/**
 * Получение текущего времени в секундах с высокой точностью
 */
static inline double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/**
 * Конвертация статуса в строку
 */
static inline const char* solution_status_to_string(SolutionStatus status) {
    switch (status) {
        case SOLUTION_STATUS_OPTIMAL:     return "OPTIMAL";
        case SOLUTION_STATUS_FEASIBLE:    return "FEASIBLE";
        case SOLUTION_STATUS_NO_SOLUTION: return "NO_SOLUTION";
        case SOLUTION_STATUS_TIMEOUT:     return "TIMEOUT";
        case SOLUTION_STATUS_INTERRUPTED: return "INTERRUPTED";
        default:                          return "UNKNOWN";
    }
}

#endif // ERDOS_TYPES_H
