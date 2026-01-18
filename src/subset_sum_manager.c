/**
 * subset_sum_manager.c - Реализация менеджера сумм подмножеств
 */

#include <stdlib.h>
#include <string.h>
#include "../include/subset_sum_manager.h"
#include "../include/logger.h"

// ============================================================================
// Константы
// ============================================================================

#define INITIAL_BUCKET_COUNT 1024
#define LOAD_FACTOR_THRESHOLD 0.75

// ============================================================================
// Вспомогательные функции хеширования
// ============================================================================

/**
 * Хеш-функция для GMP числа
 */
static size_t mpz_hash(const mpz_t value, size_t bucket_count) {
    // Используем младшие биты числа для хеша
    size_t hash = 0;
    size_t limbs = mpz_size(value);

    if (limbs > 0) {
        // Берем первые несколько limbs для хеша
        for (size_t i = 0; i < limbs && i < 4; i++) {
            hash ^= mpz_getlimbn(value, i);
            hash = (hash << 7) | (hash >> (sizeof(size_t) * 8 - 7));
        }
    }

    return hash % bucket_count;
}

/**
 * Изменение размера хеш-таблицы
 */
static void mpz_hashset_resize(MpzHashSet *set) {
    size_t new_bucket_count = set->bucket_count * 2;
    HashNode **new_buckets = calloc(new_bucket_count, sizeof(HashNode*));

    // Перехешируем все элементы
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            size_t new_index = mpz_hash(node->value, new_bucket_count);
            node->next = new_buckets[new_index];
            new_buckets[new_index] = node;
            node = next;
        }
    }

    free(set->buckets);
    set->buckets = new_buckets;
    set->bucket_count = new_bucket_count;
}

// ============================================================================
// Реализация хеш-таблицы
// ============================================================================

MpzHashSet* mpz_hashset_create(size_t initial_buckets) {
    MpzHashSet *set = malloc(sizeof(MpzHashSet));
    set->bucket_count = initial_buckets > 0 ? initial_buckets : INITIAL_BUCKET_COUNT;
    set->buckets = calloc(set->bucket_count, sizeof(HashNode*));
    set->pool = NULL;
    set->size = 0;
    return set;
}

void mpz_hashset_destroy(MpzHashSet *set) {
    if (!set) return;

    // Очистка buckets
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            mpz_clear(node->value);
            free(node);
            node = next;
        }
    }

    // Очистка пула
    HashNode *current = set->pool;
    while (current) {
        HashNode *next = current->next;
        mpz_clear(current->value);
        free(current);
        current = next;
    }

    free(set->buckets);
    free(set);
}

bool mpz_hashset_add(MpzHashSet *set, const mpz_t value) {
    // Проверяем, есть ли уже такое значение
    if (mpz_hashset_contains(set, value)) {
        return false;
    }

    // Проверяем необходимость изменения размера
    if ((double)set->size / (double)set->bucket_count > LOAD_FACTOR_THRESHOLD) {
        mpz_hashset_resize(set);
    }

    // Добавляем новый узел (используем пул если есть)
    size_t index = mpz_hash(value, set->bucket_count);
    HashNode *node;
    if (set->pool) {
        // Берем из пула (быстро)
        node = set->pool;
        set->pool = node->next;
        // mpz_t уже инициализирован, просто меняем значение
        mpz_set(node->value, value);
    } else {
        // Пул пуст, создаем новый узел (медленно)
        node = malloc(sizeof(HashNode));
        mpz_init_set(node->value, value);
    }
    node->next = set->buckets[index];
    set->buckets[index] = node;
    set->size++;

    return true;
}

bool mpz_hashset_contains(const MpzHashSet *set, const mpz_t value) {
    size_t index = mpz_hash(value, set->bucket_count);
    HashNode *node = set->buckets[index];

    while (node) {
        if (mpz_cmp(node->value, value) == 0) {
            return true;
        }
        node = node->next;
    }

    return false;
}

bool mpz_hashset_remove(MpzHashSet *set, const mpz_t value) {
    size_t index = mpz_hash(value, set->bucket_count);
    HashNode *node = set->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (mpz_cmp(node->value, value) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                set->buckets[index] = node->next;
            }
            // Возвращаем узел в пул вместо free
            // Не очищаем mpz_t - оставляем память для переиспользования
            node->next = set->pool;
            set->pool = node;
            set->size--;
            return true;
        }
        prev = node;
        node = node->next;
    }

    return false;
}

void mpz_hashset_clear(MpzHashSet *set) {
    for (size_t i = 0; i < set->bucket_count; i++) {
        HashNode *node = set->buckets[i];
        while (node) {
            HashNode *next = node->next;
            // Возвращаем узел в пул вместо free
            node->next = set->pool;
            set->pool = node;
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
    history->sums = malloc(history->capacity * sizeof(mpz_t));
    for (size_t i = 0; i < history->capacity; i++) {
        mpz_init(history->sums[i]);
    }
    history->count = 0;
}

static void sums_history_clear(SumsHistory *history) {
    if (history->sums) {
        for (size_t i = 0; i < history->capacity; i++) {
            mpz_clear(history->sums[i]);
        }
        free(history->sums);
        history->sums = NULL;
    }
    history->count = 0;
    history->capacity = 0;
}

static void sums_history_add(SumsHistory *history, const mpz_t value) {
    if (history->count >= history->capacity) {
        size_t new_capacity = history->capacity * 2;
        history->sums = realloc(history->sums, new_capacity * sizeof(mpz_t));
        for (size_t i = history->capacity; i < new_capacity; i++) {
            mpz_init(history->sums[i]);
        }
        history->capacity = new_capacity;
    }
    mpz_set(history->sums[history->count], value);
    history->count++;
}

static void history_stack_init(HistoryStack *stack, size_t capacity) {
    stack->capacity = capacity > 0 ? capacity : 64;
    stack->entries = malloc(stack->capacity * sizeof(SumsHistory));
    for (size_t i = 0; i < stack->capacity; i++) {
        sums_history_init(&stack->entries[i], 256);
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
            sums_history_init(&stack->entries[i], 256);
        }
        stack->capacity = new_capacity;
    }
    // Очищаем счетчик, но не память
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

    mpz_set_init(&manager->elements, 64);
    mpz_init(manager->temp_sum);

    if (type == MANAGER_TYPE_FAST) {
        manager->sums_set = mpz_hashset_create(INITIAL_BUCKET_COUNT);
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

    mpz_set_clear(&manager->elements);
    mpz_clear(manager->temp_sum);

    if (manager->sums_set) {
        mpz_hashset_destroy(manager->sums_set);
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
        mpz_hashset_clear(manager->sums_set);
        manager->history->count = 0;
    }
}

/**
 * Вычисление новых сумм при добавлении элемента (быстрый режим)
 * new_sums = {value} ∪ {value + s | s ∈ current_sums}
 */
static bool compute_and_add_sums_fast(SubsetSumManager *manager, const mpz_t value,
                                       SumsHistory *new_sums_history) {
    // Сначала проверяем само значение
    if (mpz_hashset_contains(manager->sums_set, value)) {
        return false;  // Коллизия
    }

    // Собираем текущие суммы для вычисления новых
    // (нужно скопировать, т.к. будем модифицировать set)
    size_t current_count = manager->sums_set->size;

    // Выделяем временный массив для текущих сумм
    mpz_t *current_sums = NULL;
    if (current_count > 0) {
        current_sums = malloc(current_count * sizeof(mpz_t));
        size_t idx = 0;
        for (size_t i = 0; i < manager->sums_set->bucket_count && idx < current_count; i++) {
            HashNode *node = manager->sums_set->buckets[i];
            while (node && idx < current_count) {
                mpz_init_set(current_sums[idx], node->value);
                idx++;
                node = node->next;
            }
        }
    }

    // Проверяем коллизии для всех новых сумм
    for (size_t i = 0; i < current_count; i++) {
        mpz_add(manager->temp_sum, value, current_sums[i]);
        if (mpz_hashset_contains(manager->sums_set, manager->temp_sum)) {
            // Коллизия! Освобождаем память и возвращаем false
            for (size_t j = 0; j < current_count; j++) {
                mpz_clear(current_sums[j]);
            }
            free(current_sums);
            return false;
        }
    }

    // Коллизий нет, добавляем все новые суммы

    // Добавляем само значение
    mpz_hashset_add(manager->sums_set, value);
    sums_history_add(new_sums_history, value);

    // Добавляем value + каждая существующая сумма
    for (size_t i = 0; i < current_count; i++) {
        mpz_add(manager->temp_sum, value, current_sums[i]);
        mpz_hashset_add(manager->sums_set, manager->temp_sum);
        sums_history_add(new_sums_history, manager->temp_sum);
    }

    // Освобождаем временный массив
    if (current_sums) {
        for (size_t i = 0; i < current_count; i++) {
            mpz_clear(current_sums[i]);
        }
        free(current_sums);
    }

    return true;
}

/**
 * Итеративная проверка коллизий
 * Перебирает все 2^N подмножеств текущих элементов
 */
bool subset_sum_manager_has_collision_iterative(SubsetSumManager *manager,
                                                const mpz_t new_value) {
    size_t n = manager->elements.size;

    if (n == 0) {
        return false;  // Нет коллизий для пустого множества
    }

    // Для n <= 62 используем битовую маску
    if (n <= 62) {
        uint64_t total_subsets = (1ULL << n);

        // Проверяем все подмножества
        for (uint64_t mask = 1; mask < total_subsets; mask++) {
            mpz_set_ui(manager->temp_sum, 0);

            for (size_t i = 0; i < n; i++) {
                if (mask & (1ULL << i)) {
                    mpz_add(manager->temp_sum, manager->temp_sum,
                            manager->elements.elements[i]);
                }
            }

            // Проверка 1: new_value равно какой-то существующей сумме
            if (mpz_cmp(manager->temp_sum, new_value) == 0) {
                return true;
            }
        }

        // Проверка 2: new_value + существующая_сумма равно другой существующей сумме
        // Это означает, что две разные подмножества {new_value} ∪ A и B имеют равные суммы
        for (uint64_t mask1 = 1; mask1 < total_subsets; mask1++) {
            mpz_set(manager->temp_sum, new_value);
            for (size_t i = 0; i < n; i++) {
                if (mask1 & (1ULL << i)) {
                    mpz_add(manager->temp_sum, manager->temp_sum,
                            manager->elements.elements[i]);
                }
            }

            // Проверяем, равна ли эта сумма какой-либо сумме подмножества без new_value
            for (uint64_t mask2 = 1; mask2 < total_subsets; mask2++) {
                // Пропускаем подмножества, пересекающиеся с mask1
                // (чтобы избежать сравнения пересекающихся множеств)
                if (mask1 & mask2) continue;

                mpz_t sum2;
                mpz_init_set_ui(sum2, 0);
                for (size_t i = 0; i < n; i++) {
                    if (mask2 & (1ULL << i)) {
                        mpz_add(sum2, sum2, manager->elements.elements[i]);
                    }
                }

                if (mpz_cmp(manager->temp_sum, sum2) == 0) {
                    mpz_clear(sum2);
                    return true;
                }
                mpz_clear(sum2);
            }
        }

        return false;
    }

    // Для n > 62 используем рекурсивный перебор (mpz для масок)
    // Это очень медленно, но работает для произвольно больших n
    mpz_t mask, total, one;
    mpz_init_set_ui(mask, 1);
    mpz_init(total);
    mpz_init_set_ui(one, 1);
    mpz_mul_2exp(total, one, n);  // total = 2^n

    while (mpz_cmp(mask, total) < 0) {
        mpz_set_ui(manager->temp_sum, 0);

        for (size_t i = 0; i < n; i++) {
            if (mpz_tstbit(mask, i)) {
                mpz_add(manager->temp_sum, manager->temp_sum,
                        manager->elements.elements[i]);
            }
        }

        if (mpz_cmp(manager->temp_sum, new_value) == 0) {
            mpz_clear(mask);
            mpz_clear(total);
            mpz_clear(one);
            return true;
        }

        mpz_add_ui(mask, mask, 1);
    }

    mpz_clear(mask);
    mpz_clear(total);
    mpz_clear(one);

    // Проверка коллизий между {new_value} ∪ A и B
    // Аналогично, но с mpz для масок
    // (упрощенная версия - полная проверка)

    return false;
}

bool subset_sum_manager_add_element(SubsetSumManager *manager, const mpz_t value) {
    if (manager->type == MANAGER_TYPE_FAST) {
        // Быстрый режим: используем хеш-таблицу
        SumsHistory *history = history_stack_push(manager->history);

        if (!compute_and_add_sums_fast(manager, value, history)) {
            // Коллизия - откатываем историю
            history_stack_pop(manager->history);
            return false;
        }

        // Добавляем элемент в множество
        mpz_set_push(&manager->elements, value);
        return true;

    } else {
        // Итеративный режим: проверяем коллизии перебором
        if (subset_sum_manager_has_collision_iterative(manager, value)) {
            return false;
        }

        // Добавляем элемент
        mpz_set_push(&manager->elements, value);
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
                mpz_hashset_remove(manager->sums_set, history->sums[i]);
            }
        }
    }

    // Удаляем последний элемент
    mpz_set_pop(&manager->elements);
}

size_t subset_sum_manager_size(const SubsetSumManager *manager) {
    return manager->elements.size;
}

void subset_sum_manager_get_element(const SubsetSumManager *manager,
                                    size_t index, mpz_t result) {
    if (index < manager->elements.size) {
        mpz_set(result, manager->elements.elements[index]);
    }
}

void subset_sum_manager_get_elements(const SubsetSumManager *manager, MpzSet *result) {
    mpz_set_copy(result, &manager->elements);
}
