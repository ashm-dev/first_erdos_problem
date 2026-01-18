/**
 * subset_sum_manager.h - Менеджер сумм подмножеств
 *
 * Два режима работы:
 * 1. Быстрый (Fast) - хранит все суммы в хеш-таблице, O(1) проверка коллизий
 * 2. Итеративный (Iterative) - не хранит суммы, O(2^N) проверка коллизий
 */

#ifndef ERDOS_SUBSET_SUM_MANAGER_H
#define ERDOS_SUBSET_SUM_MANAGER_H

#include <stdbool.h>
#include <gmp.h>
#include "types.h"

// ============================================================================
// Структуры данных для хеш-таблицы (быстрый режим)
// ============================================================================

/**
 * Узел хеш-таблицы
 */
typedef struct HashNode {
    mpz_t value;
    struct HashNode *next;
} HashNode;

/**
 * Хеш-таблица для хранения сумм
 */
typedef struct {
    HashNode **buckets;
    HashNode *pool;      // Список переиспользуемых узлов (Object Pool)
    size_t bucket_count;
    size_t size;
} MpzHashSet;

/**
 * История добавленных сумм (для отката)
 */
typedef struct {
    mpz_t *sums;
    size_t count;
    size_t capacity;
} SumsHistory;

/**
 * Стек истории (для каждого добавленного элемента)
 */
typedef struct {
    SumsHistory *entries;
    size_t count;
    size_t capacity;
} HistoryStack;

// ============================================================================
// Основная структура менеджера
// ============================================================================

/**
 * Менеджер сумм подмножеств
 */
typedef struct {
    ManagerType type;

    // Текущие элементы множества
    MpzSet elements;

    // Для быстрого режима
    MpzHashSet *sums_set;        // Все текущие суммы
    HistoryStack *history;       // История для отката

    // Временные переменные (для избежания повторных аллокаций)
    mpz_t temp_sum;
} SubsetSumManager;

// ============================================================================
// Функции работы с хеш-таблицей
// ============================================================================

/**
 * Создание хеш-таблицы
 */
MpzHashSet* mpz_hashset_create(size_t initial_buckets);

/**
 * Освобождение хеш-таблицы
 */
void mpz_hashset_destroy(MpzHashSet *set);

/**
 * Добавление значения в хеш-таблицу
 */
bool mpz_hashset_add(MpzHashSet *set, const mpz_t value);

/**
 * Проверка наличия значения
 */
bool mpz_hashset_contains(const MpzHashSet *set, const mpz_t value);

/**
 * Удаление значения из хеш-таблицы
 */
bool mpz_hashset_remove(MpzHashSet *set, const mpz_t value);

/**
 * Очистка хеш-таблицы
 */
void mpz_hashset_clear(MpzHashSet *set);

// ============================================================================
// Функции менеджера сумм
// ============================================================================

/**
 * Создание менеджера
 */
SubsetSumManager* subset_sum_manager_create(ManagerType type);

/**
 * Освобождение менеджера
 */
void subset_sum_manager_destroy(SubsetSumManager *manager);

/**
 * Сброс менеджера (очистка всех данных)
 */
void subset_sum_manager_reset(SubsetSumManager *manager);

/**
 * Попытка добавить элемент
 * Возвращает true если элемент добавлен (нет коллизий),
 * false если есть коллизия (элемент не добавлен)
 */
bool subset_sum_manager_add_element(SubsetSumManager *manager, const mpz_t value);

/**
 * Удаление последнего добавленного элемента (откат)
 */
void subset_sum_manager_remove_last(SubsetSumManager *manager);

/**
 * Получение текущего количества элементов
 */
size_t subset_sum_manager_size(const SubsetSumManager *manager);

/**
 * Получение элемента по индексу
 */
void subset_sum_manager_get_element(const SubsetSumManager *manager,
                                    size_t index, mpz_t result);

/**
 * Получение копии текущего множества
 */
void subset_sum_manager_get_elements(const SubsetSumManager *manager, MpzSet *result);

// ============================================================================
// Внутренние функции (для итеративного режима)
// ============================================================================

/**
 * Проверка коллизии для нового элемента (итеративный режим)
 * Перебирает все подмножества текущих элементов
 */
bool subset_sum_manager_has_collision_iterative(SubsetSumManager *manager,
                                                const mpz_t new_value);

#endif // ERDOS_SUBSET_SUM_MANAGER_H
