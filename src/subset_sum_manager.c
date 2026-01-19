/**
 * subset_sum_manager.c - Высокопроизводительная реализация менеджера сумм
 *
 * Использует нативную арифметику uint64_t вместо GMP.
 */

#include <stdlib.h>
#include <string.h>
#include "../include/subset_sum_manager.h"
#include "../include/logger.h"

// ============================================================================
// Константы
// ============================================================================

#define INITIAL_BUCKET_COUNT 4096
#define LOAD_FACTOR_THRESHOLD 0.75
#define POOL_PREALLOC_SIZE 1024

// ============================================================================
// Быстрая хеш-функция (Murmur3 finalizer)
// ============================================================================

static inline size_t int_hash(value_t x, size_t bucket_count) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)(x % bucket_count);
}

// ============================================================================
// Реализация хеш-таблицы
// ============================================================================

/**
 * Предаллокация пула узлов
 */
static void int_hashset_prealloc_pool(IntHashSet *set, size_t count) {
    for (size_t i = 0; i < count; i++) {
        HashNode *node = malloc(sizeof(HashNode));
        node->next = set->pool;
        set->pool = node;
    }
}

/**
 * Взять узел из пула или создать новый
 */
static inline HashNode* pool_get_node(IntHashSet *set) {
    if (set->pool) {
        HashNode *node = set->pool;
        set->pool = node->next;
        return node;
    }
    return malloc(sizeof(HashNode));
}

/**
 * Вернуть узел в пул
 */
static inline void pool_return_node(IntHashSet *set, HashNode *node) {
    node->next = set->pool;
    set->pool = node;
}

/**
 * Изменение размера хеш-таблицы
 */
static void int_hashset_resize(IntHashSet *set) {
    size_t new_bucket_count = set->bucket_count * 2;
    HashNode **new_buckets = calloc(new_bucket_count, sizeof(HashNode*));

    // Перехешируем все элементы
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            size_t new_index = int_hash(node->value, new_bucket_count);
            node->next = new_buckets[new_index];
            new_buckets[new_index] = node;
            node = next;
        }
    }

    free(set->buckets);
    set->buckets = new_buckets;
    set->bucket_count = new_bucket_count;
}

IntHashSet* int_hashset_create(size_t initial_buckets) {
    IntHashSet *set = malloc(sizeof(IntHashSet));
    set->bucket_count = initial_buckets > 0 ? initial_buckets : INITIAL_BUCKET_COUNT;
    set->buckets = calloc(set->bucket_count, sizeof(HashNode*));
    set->pool = NULL;
    set->size = 0;

    // Предаллокация пула для избежания malloc в горячем пути
    int_hashset_prealloc_pool(set, POOL_PREALLOC_SIZE);

    return set;
}

void int_hashset_destroy(IntHashSet *set) {
    if (!set) return;

    // Очистка buckets
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            free(node);
            node = next;
        }
    }

    // Очистка пула
    HashNode *current = set->pool;
    while (current) {
        HashNode *next = current->next;
        free(current);
        current = next;
    }

    free(set->buckets);
    free(set);
}

bool int_hashset_add(IntHashSet *set, value_t value) {
    // Проверяем, есть ли уже такое значение
    if (int_hashset_contains(set, value)) {
        return false;
    }

    // Проверяем необходимость изменения размера
    if ((double)set->size / (double)set->bucket_count > LOAD_FACTOR_THRESHOLD) {
        int_hashset_resize(set);
    }

    // Добавляем новый узел
    size_t index = int_hash(value, set->bucket_count);
    HashNode *node = pool_get_node(set);
    node->value = value;
    node->next = set->buckets[index];
    set->buckets[index] = node;
    set->size++;

    return true;
}

bool int_hashset_contains(const IntHashSet *set, value_t value) {
    size_t index = int_hash(value, set->bucket_count);
    HashNode *node = set->buckets[index];

    while (node) {
        if (node->value == value) {
            return true;
        }
        node = node->next;
    }

    return false;
}

bool int_hashset_remove(IntHashSet *set, value_t value) {
    size_t index = int_hash(value, set->bucket_count);
    HashNode *node = set->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (node->value == value) {
            if (prev) {
                prev->next = node->next;
            } else {
                set->buckets[index] = node->next;
            }
            // Возвращаем узел в пул
            pool_return_node(set, node);
            set->size--;
            return true;
        }
        prev = node;
        node = node->next;
    }

    return false;
}

void int_hashset_clear(IntHashSet *set) {
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            pool_return_node(set, node);
            node = next;
        }
        set->buckets[i] = NULL;
    }
    set->size = 0;
}

// ============================================================================
// Реализация истории для отката
// ============================================================================

static void sums_history_init(SumsHistory *history, size_t capacity) {
    history->capacity = capacity > 0 ? capacity : 256;
    history->sums = malloc(history->capacity * sizeof(value_t));
    history->count = 0;
}

static void sums_history_clear(SumsHistory *history) {
    if (history->sums) {
        free(history->sums);
        history->sums = NULL;
    }
    history->count = 0;
    history->capacity = 0;
}

static inline void sums_history_add(SumsHistory *history, value_t value) {
    if (history->count >= history->capacity) {
        history->capacity *= 2;
        history->sums = realloc(history->sums, history->capacity * sizeof(value_t));
    }
    history->sums[history->count++] = value;
}

static void history_stack_init(HistoryStack *stack, size_t capacity) {
    stack->capacity = capacity > 0 ? capacity : 64;
    stack->entries = malloc(stack->capacity * sizeof(SumsHistory));
    for (size_t i = 0; i < stack->capacity; i++) {
        sums_history_init(&stack->entries[i], 512);
    }
    stack->count = 0;
}

static void history_stack_clear(HistoryStack *stack) {
    if (stack->entries) {
        for (size_t i = 0; i < stack->capacity; i++) {
            sums_history_clear(&stack->entries[i]);
        }
        free(stack->entries);
        stack->entries = NULL;
    }
    stack->count = 0;
    stack->capacity = 0;
}

static SumsHistory* history_stack_push(HistoryStack *stack) {
    if (stack->count >= stack->capacity) {
        size_t new_capacity = stack->capacity * 2;
        stack->entries = realloc(stack->entries, new_capacity * sizeof(SumsHistory));
        for (size_t i = stack->capacity; i < new_capacity; i++) {
            sums_history_init(&stack->entries[i], 512);
        }
        stack->capacity = new_capacity;
    }
    stack->entries[stack->count].count = 0;
    return &stack->entries[stack->count++];
}

static SumsHistory* history_stack_pop(HistoryStack *stack) {
    if (stack->count == 0) return NULL;
    return &stack->entries[--stack->count];
}

// ============================================================================
// Реализация менеджера сумм
// ============================================================================

SubsetSumManager* subset_sum_manager_create(ManagerType type) {
    SubsetSumManager *manager = malloc(sizeof(SubsetSumManager));
    manager->type = type;

    number_set_init(&manager->elements, 64);
    manager->temp_sum = 0;

    if (type == MANAGER_TYPE_FAST) {
        manager->sums_set = int_hashset_create(INITIAL_BUCKET_COUNT);
        manager->history = malloc(sizeof(HistoryStack));
        history_stack_init(manager->history, 64);
    } else {
        manager->sums_set = NULL;
        manager->history = NULL;
    }

    return manager;
}

void subset_sum_manager_destroy(SubsetSumManager *manager) {
    if (!manager) return;

    number_set_clear(&manager->elements);

    if (manager->sums_set) {
        int_hashset_destroy(manager->sums_set);
    }

    if (manager->history) {
        history_stack_clear(manager->history);
        free(manager->history);
    }

    free(manager);
}

void subset_sum_manager_reset(SubsetSumManager *manager) {
    manager->elements.size = 0;

    if (manager->type == MANAGER_TYPE_FAST) {
        int_hashset_clear(manager->sums_set);
        manager->history->count = 0;
    }
}

/**
 * Вычисление новых сумм при добавлении элемента (быстрый режим)
 * new_sums = {value} ∪ {value + s | s ∈ current_sums}
 *
 * Оптимизация: сначала собираем все текущие суммы в массив,
 * затем проверяем все коллизии, и только потом добавляем.
 */
static bool compute_and_add_sums_fast(SubsetSumManager *manager, value_t value,
                                       SumsHistory *new_sums_history) {
    // Проверяем само значение на коллизию
    if (int_hashset_contains(manager->sums_set, value)) {
        return false;
    }

    size_t current_count = manager->sums_set->size;

    // Собираем текущие суммы в массив (нужно, т.к. будем модифицировать set)
    value_t *current_sums = NULL;
    if (current_count > 0) {
        current_sums = malloc(current_count * sizeof(value_t));
        size_t idx = 0;
        for (size_t i = 0; i < manager->sums_set->bucket_count && idx < current_count; i++) {
            HashNode *node = manager->sums_set->buckets[i];
            while (node && idx < current_count) {
                current_sums[idx++] = node->value;
                node = node->next;
            }
        }
    }

    // Проверяем коллизии для всех новых сумм
    for (size_t i = 0; i < current_count; i++) {
        value_t new_sum = value + current_sums[i];
        if (int_hashset_contains(manager->sums_set, new_sum)) {
            free(current_sums);
            return false;
        }
    }

    // Проверяем коллизии между новыми суммами
    // (value + sum_i) == (value + sum_j) невозможно при разных sum_i, sum_j
    // Но нужно проверить value == (value + sum_i) - невозможно при sum_i > 0
    // И (value + sum_i) == value - тоже невозможно

    // Коллизий нет — добавляем все новые суммы

    // Добавляем само значение
    int_hashset_add(manager->sums_set, value);
    sums_history_add(new_sums_history, value);

    // Добавляем value + каждая существующая сумма
    for (size_t i = 0; i < current_count; i++) {
        value_t new_sum = value + current_sums[i];
        int_hashset_add(manager->sums_set, new_sum);
        sums_history_add(new_sums_history, new_sum);
    }

    free(current_sums);
    return true;
}

/**
 * Итеративная проверка коллизий
 * Перебирает все 2^N подмножеств текущих элементов
 */
bool subset_sum_manager_has_collision_iterative(SubsetSumManager *manager,
                                                value_t new_value) {
    size_t n = manager->elements.size;

    if (n == 0) {
        return false;
    }

    // Для n <= 62 используем битовую маску
    if (n > 62) {
        LOG_ERROR("Итеративный режим не поддерживает n > 62");
        return true;  // Безопасный отказ
    }

    uint64_t total_subsets = (1ULL << n);

    // Проверка 1: new_value равно какой-то существующей сумме подмножества
    for (uint64_t mask = 1; mask < total_subsets; mask++) {
        value_t sum = 0;
        for (size_t i = 0; i < n; i++) {
            if (mask & (1ULL << i)) {
                sum += manager->elements.elements[i];
            }
        }
        if (sum == new_value) {
            return true;
        }
    }

    // Проверка 2: {new_value} ∪ A и B имеют равные суммы
    // То есть new_value + sum(A) == sum(B), где A и B - непересекающиеся подмножества
    for (uint64_t mask1 = 0; mask1 < total_subsets; mask1++) {
        value_t sum1 = new_value;
        for (size_t i = 0; i < n; i++) {
            if (mask1 & (1ULL << i)) {
                sum1 += manager->elements.elements[i];
            }
        }

        // Ищем подмножество B, не пересекающееся с A, с такой же суммой
        for (uint64_t mask2 = 1; mask2 < total_subsets; mask2++) {
            // Пропускаем пересекающиеся множества
            if (mask1 & mask2) continue;

            value_t sum2 = 0;
            for (size_t i = 0; i < n; i++) {
                if (mask2 & (1ULL << i)) {
                    sum2 += manager->elements.elements[i];
                }
            }

            if (sum1 == sum2) {
                return true;
            }
        }
    }

    return false;
}

bool subset_sum_manager_add_element(SubsetSumManager *manager, value_t value) {
    if (manager->type == MANAGER_TYPE_FAST) {
        // Быстрый режим: используем хеш-таблицу
        SumsHistory *history = history_stack_push(manager->history);

        if (!compute_and_add_sums_fast(manager, value, history)) {
            // Коллизия - откатываем историю
            history_stack_pop(manager->history);
            return false;
        }

        // Добавляем элемент в множество
        number_set_push(&manager->elements, value);
        return true;

    } else {
        // Итеративный режим: проверяем коллизии перебором
        if (subset_sum_manager_has_collision_iterative(manager, value)) {
            return false;
        }

        // Добавляем элемент
        number_set_push(&manager->elements, value);
        return true;
    }
}

void subset_sum_manager_remove_last(SubsetSumManager *manager) {
    if (manager->elements.size == 0) return;

    if (manager->type == MANAGER_TYPE_FAST) {
        // Откатываем добавленные суммы из истории
        SumsHistory *history = history_stack_pop(manager->history);
        if (history) {
            for (size_t i = 0; i < history->count; i++) {
                int_hashset_remove(manager->sums_set, history->sums[i]);
            }
        }
    }

    // Удаляем последний элемент
    number_set_pop(&manager->elements);
}

size_t subset_sum_manager_size(const SubsetSumManager *manager) {
    return manager->elements.size;
}

value_t subset_sum_manager_get_element(const SubsetSumManager *manager, size_t index) {
    if (index < manager->elements.size) {
        return manager->elements.elements[index];
    }
    return 0;
}

void subset_sum_manager_get_elements(const SubsetSumManager *manager, NumberSet *result) {
    number_set_copy(result, &manager->elements);
}
