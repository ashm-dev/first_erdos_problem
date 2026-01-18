/**
 * types.h - Типы данных и константы для Erdos Solver
 *
 * Реализация на C23 с GMP для работы с произвольно большими числами.
 */

#ifndef ERDOS_TYPES_H
#define ERDOS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gmp.h>

// ============================================================================
// Константы
// ============================================================================

#define ERDOS_MAX_SET_SIZE 1024
#define ERDOS_DEFAULT_DB_PATH "erdos_results.db"
#define ERDOS_LOG_INTERVAL_SEC 60

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
 * Множество GMP чисел (элементы решения)
 */
typedef struct {
    mpz_t *elements;      // Массив элементов
    size_t size;          // Текущее количество элементов
    size_t capacity;      // Выделенная емкость
} MpzSet;

/**
 * Результат решения
 */
typedef struct {
    uint32_t n;                   // Размер множества
    mpz_t max_value;              // Максимальный элемент
    MpzSet solution_set;          // Найденное множество
    double computation_time;       // Время вычисления в секундах
    SolutionStatus status;         // Статус решения
    uint64_t nodes_explored;       // Количество исследованных узлов
    time_t timestamp;              // Время завершения
} SolutionResult;

/**
 * Конфигурация решателя
 */
typedef struct {
    uint32_t n;                    // Размер искомого множества
    mpz_t initial_bound;           // Начальная верхняя граница (0 = авто)
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
    mpz_t best_max;                // Лучший найденный максимум
    uint32_t solutions_found;      // Количество найденных решений
    time_t start_time;             // Время начала
    time_t last_log_time;          // Время последнего лога
} SearchStats;

// ============================================================================
// Функции работы с MpzSet
// ============================================================================

/**
 * Инициализация множества
 */
static inline void mpz_set_init(MpzSet *set, size_t initial_capacity) {
    set->capacity = initial_capacity > 0 ? initial_capacity : 16;
    set->size = 0;
    set->elements = malloc(set->capacity * sizeof(mpz_t));
    for (size_t i = 0; i < set->capacity; i++) {
        mpz_init(set->elements[i]);
    }
}

/**
 * Освобождение памяти множества
 */
static inline void mpz_set_clear(MpzSet *set) {
    if (set->elements) {
        for (size_t i = 0; i < set->capacity; i++) {
            mpz_clear(set->elements[i]);
        }
        free(set->elements);
        set->elements = NULL;
    }
    set->size = 0;
    set->capacity = 0;
}

/**
 * Добавление элемента в множество
 */
static inline void mpz_set_push(MpzSet *set, const mpz_t value) {
    if (set->size >= set->capacity) {
        size_t new_capacity = set->capacity * 2;
        set->elements = realloc(set->elements, new_capacity * sizeof(mpz_t));
        for (size_t i = set->capacity; i < new_capacity; i++) {
            mpz_init(set->elements[i]);
        }
        set->capacity = new_capacity;
    }
    mpz_set(set->elements[set->size], value);
    set->size++;
}

/**
 * Удаление последнего элемента
 */
static inline void mpz_set_pop(MpzSet *set) {
    if (set->size > 0) {
        set->size--;
    }
}

/**
 * Копирование множества
 */
static inline void mpz_set_copy(MpzSet *dest, const MpzSet *src) {
    mpz_set_clear(dest);
    mpz_set_init(dest, src->capacity);
    for (size_t i = 0; i < src->size; i++) {
        mpz_set_push(dest, src->elements[i]);
    }
}

/**
 * Получение строкового представления множества
 * Возвращает динамически выделенную строку (нужно освободить)
 */
static inline char* mpz_set_to_string(const MpzSet *set) {
    if (set->size == 0) {
        char *result = malloc(3);
        strcpy(result, "{}");
        return result;
    }

    // Оценка размера буфера
    size_t buf_size = 3; // "{}"
    for (size_t i = 0; i < set->size; i++) {
        buf_size += mpz_sizeinbase(set->elements[i], 10) + 2; // число + ", "
    }

    char *result = malloc(buf_size);
    char *ptr = result;
    *ptr++ = '{';

    for (size_t i = 0; i < set->size; i++) {
        if (i > 0) {
            *ptr++ = ',';
            *ptr++ = ' ';
        }
        char *num_str = mpz_get_str(NULL, 10, set->elements[i]);
        size_t len = strlen(num_str);
        memcpy(ptr, num_str, len);
        ptr += len;
        free(num_str);
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
    mpz_init(result->max_value);
    mpz_set_init(&result->solution_set, 16);
    result->computation_time = 0.0;
    result->status = SOLUTION_STATUS_NO_SOLUTION;
    result->nodes_explored = 0;
    result->timestamp = 0;
}

/**
 * Освобождение памяти результата
 */
static inline void solution_result_clear(SolutionResult *result) {
    mpz_clear(result->max_value);
    mpz_set_clear(&result->solution_set);
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
