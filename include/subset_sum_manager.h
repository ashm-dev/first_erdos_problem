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
#include "types.h"

// ============================================================================
// Структуры данных для хеш-таблицы (быстрый режим)
// ============================================================================

/**
 * Узел хеш-таблицы
 */
typedef struct HashNode {
    value_t value;
    struct HashNode *next;
} HashNode;

/**
 * Хеш-таблица для хранения сумм
 */
typedef struct {
    HashNode **buckets;
    HashNode *pool;      // Пул переиспользуемых узлов
    size_t bucket_count;
    size_t size;
} IntHashSet;

/**
 * История добавленных сумм (для отката)
 */
typedef struct {
    value_t *sums;
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
    NumberSet elements;

    // Для быстрого режима
    IntHashSet *sums_set;        // Все текущие суммы
    HistoryStack *history;       // История для отката

    // Временная переменная для итеративного режима
    value_t temp_sum;
} SubsetSumManager;

// ============================================================================
// Функции работы с хеш-таблицей
// ============================================================================

/**
 * Создание хеш-таблицы
 */
IntHashSet* int_hashset_create(size_t initial_buckets);

/**
 * Освобождение хеш-таблицы
 */
void int_hashset_destroy(IntHashSet *set);

/**
 * Добавление значения в хеш-таблицу
 * Возвращает true если добавлено (значения не было), false если уже есть
 */
bool int_hashset_add(IntHashSet *set, value_t value);

/**
 * Проверка наличия значения
 */
bool int_hashset_contains(const IntHashSet *set, value_t value);

/**
 * Удаление значения из хеш-таблицы
 */
bool int_hashset_remove(IntHashSet *set, value_t value);

/**
 * Очистка хеш-таблицы
 */
void int_hashset_clear(IntHashSet *set);

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
bool subset_sum_manager_add_element(SubsetSumManager *manager, value_t value);

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
value_t subset_sum_manager_get_element(const SubsetSumManager *manager, size_t index);

/**
 * Получение копии текущего множества
 */
void subset_sum_manager_get_elements(const SubsetSumManager *manager, NumberSet *result);

// ============================================================================
// Внутренние функции (для итеративного режима)
// ============================================================================

/**
 * Проверка коллизии для нового элемента (итеративный режим)
 * Перебирает все подмножества текущих элементов
 */
bool subset_sum_manager_has_collision_iterative(SubsetSumManager *manager,
                                                value_t new_value);

#endif // ERDOS_SUBSET_SUM_MANAGER_H
