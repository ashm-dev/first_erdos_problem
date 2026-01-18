/**
 * db_manager.c - Реализация менеджера базы данных SQLite
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/db_manager.h"
#include "../include/logger.h"

// ============================================================================
// SQL запросы
// ============================================================================

static const char *SQL_CREATE_TABLES =
    "CREATE TABLE IF NOT EXISTS schema_version ("
    "    version INTEGER PRIMARY KEY"
    ");"
    "INSERT OR IGNORE INTO schema_version (version) VALUES (1);"
    ""
    "CREATE TABLE IF NOT EXISTS results ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    n INTEGER NOT NULL,"
    "    max_value TEXT NOT NULL,"
    "    solution_set TEXT NOT NULL,"
    "    computation_time REAL NOT NULL,"
    "    status TEXT NOT NULL,"
    "    nodes_explored INTEGER NOT NULL,"
    "    timestamp INTEGER NOT NULL,"
    "    UNIQUE(n, max_value, solution_set)"
    ");"
    ""
    "CREATE INDEX IF NOT EXISTS idx_results_n ON results(n);"
    "CREATE INDEX IF NOT EXISTS idx_results_status ON results(status);"
    ""
    "CREATE TABLE IF NOT EXISTS optimal_sets ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    n INTEGER NOT NULL,"
    "    max_value TEXT NOT NULL,"
    "    solution_set TEXT NOT NULL,"
    "    UNIQUE(n, solution_set)"
    ");"
    ""
    "CREATE INDEX IF NOT EXISTS idx_optimal_n ON optimal_sets(n);";

static const char *SQL_INSERT_RESULT =
    "INSERT OR REPLACE INTO results "
    "(n, max_value, solution_set, computation_time, status, nodes_explored, timestamp) "
    "VALUES (?, ?, ?, ?, ?, ?, ?);";

static const char *SQL_INSERT_OPTIMAL =
    "INSERT OR IGNORE INTO optimal_sets (n, max_value, solution_set) "
    "VALUES (?, ?, ?);";

static const char *SQL_SELECT_RESULT =
    "SELECT max_value, solution_set, computation_time, status, nodes_explored, timestamp "
    "FROM results WHERE n = ? AND status = 'OPTIMAL' "
    "ORDER BY CAST(max_value AS INTEGER) ASC LIMIT 1;";

static const char *SQL_SELECT_BEST_BOUND =
    "SELECT MIN(CAST(max_value AS INTEGER)) FROM results WHERE n = ?;";

static const char *SQL_HAS_OPTIMAL =
    "SELECT 1 FROM results WHERE n = ? AND status = 'OPTIMAL' LIMIT 1;";

static const char *SQL_LAST_N =
    "SELECT MAX(n) FROM results WHERE status = 'OPTIMAL';";

static const char *SQL_SELECT_OPTIMAL_SETS =
    "SELECT solution_set FROM optimal_sets WHERE n = ?;";

static const char *SQL_SELECT_ALL_RESULTS =
    "SELECT n, max_value, solution_set, computation_time, status, nodes_explored, timestamp "
    "FROM results ORDER BY n ASC;";

static const char *SQL_SELECT_SUMMARY =
    "SELECT n, MIN(max_value) as max_value, COUNT(*) as count, "
    "SUM(computation_time) as total_time, status "
    "FROM results WHERE status = 'OPTIMAL' "
    "GROUP BY n ORDER BY n ASC;";

static const char *SQL_GET_STATS =
    "SELECT COUNT(*) as total, "
    "(SELECT COUNT(*) FROM results WHERE status = 'OPTIMAL') as optimal, "
    "(SELECT MAX(n) FROM results WHERE status = 'OPTIMAL') as max_n, "
    "(SELECT SUM(computation_time) FROM results) as total_time "
    "FROM results;";

// ============================================================================
// Вспомогательные функции
// ============================================================================

/**
 * Сериализация множества в строку JSON
 */
static char* serialize_mpz_set(const MpzSet *set) {
    if (set->size == 0) {
        char *result = malloc(3);
        strcpy(result, "[]");
        return result;
    }

    // Оценка размера
    size_t buf_size = 3;
    for (size_t i = 0; i < set->size; i++) {
        buf_size += mpz_sizeinbase(set->elements[i], 10) + 3;
    }

    char *result = malloc(buf_size);
    char *ptr = result;
    *ptr++ = '[';

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

    *ptr++ = ']';
    *ptr = '\0';

    return result;
}

/**
 * Десериализация строки JSON в множество
 */
static void deserialize_mpz_set(const char *str, MpzSet *set) {
    mpz_set_clear(set);
    mpz_set_init(set, 16);

    if (!str || strlen(str) < 2) return;

    // Пропускаем '[' и пробелы
    const char *ptr = str;
    while (*ptr && (*ptr == '[' || *ptr == ' ')) ptr++;

    mpz_t value;
    mpz_init(value);

    while (*ptr && *ptr != ']') {
        // Пропускаем пробелы и запятые
        while (*ptr && (*ptr == ' ' || *ptr == ',')) ptr++;
        if (*ptr == ']' || !*ptr) break;

        // Читаем число
        const char *start = ptr;
        while (*ptr && *ptr != ',' && *ptr != ']' && *ptr != ' ') ptr++;

        char *num_str = strndup(start, (size_t)(ptr - start));
        if (mpz_set_str(value, num_str, 10) == 0) {
            mpz_set_push(set, value);
        }
        free(num_str);
    }

    mpz_clear(value);
}

// ============================================================================
// Функции инициализации
// ============================================================================

DatabaseManager* db_manager_create(const char *db_path) {
    DatabaseManager *manager = malloc(sizeof(DatabaseManager));
    manager->db_path = strdup(db_path ? db_path : ERDOS_DEFAULT_DB_PATH);
    manager->db = NULL;
    manager->initialized = false;
    pthread_mutex_init(&manager->mutex, NULL);

    // Открываем базу данных
    int rc = sqlite3_open(manager->db_path, &manager->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Не удалось открыть БД %s: %s", manager->db_path, sqlite3_errmsg(manager->db));
        free(manager->db_path);
        free(manager);
        return NULL;
    }

    // Включаем WAL режим
    sqlite3_exec(manager->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(manager->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    // Создаем таблицы
    char *err_msg = NULL;
    rc = sqlite3_exec(manager->db, SQL_CREATE_TABLES, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Ошибка создания таблиц: %s", err_msg);
        sqlite3_free(err_msg);
    }

    manager->initialized = true;
    LOG_INFO("База данных инициализирована: %s", manager->db_path);

    return manager;
}

void db_manager_destroy(DatabaseManager *manager) {
    if (!manager) return;

    pthread_mutex_lock(&manager->mutex);

    if (manager->db) {
        sqlite3_close(manager->db);
        manager->db = NULL;
    }

    if (manager->db_path) {
        free(manager->db_path);
        manager->db_path = NULL;
    }

    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);

    free(manager);
}

// ============================================================================
// Функции сохранения
// ============================================================================

bool db_manager_save_result(DatabaseManager *manager, const SolutionResult *result) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_INSERT_RESULT, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Ошибка подготовки запроса: %s", sqlite3_errmsg(manager->db));
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    char *max_value_str = mpz_get_str(NULL, 10, result->max_value);
    char *solution_str = serialize_mpz_set(&result->solution_set);

    sqlite3_bind_int(stmt, 1, (int)result->n);
    sqlite3_bind_text(stmt, 2, max_value_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, solution_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, result->computation_time);
    sqlite3_bind_text(stmt, 5, solution_status_to_string(result->status), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)result->nodes_explored);
    sqlite3_bind_int64(stmt, 7, result->timestamp);

    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE);

    if (!success) {
        LOG_ERROR("Ошибка сохранения результата: %s", sqlite3_errmsg(manager->db));
    }

    sqlite3_finalize(stmt);
    free(max_value_str);
    free(solution_str);

    pthread_mutex_unlock(&manager->mutex);
    return success;
}

bool db_manager_save_optimal_sets(DatabaseManager *manager, uint32_t n,
                                  const MpzSet *sets, size_t count) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_exec(manager->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_INSERT_OPTIMAL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Ошибка подготовки запроса: %s", sqlite3_errmsg(manager->db));
        sqlite3_exec(manager->db, "ROLLBACK;", NULL, NULL, NULL);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    bool success = true;
    for (size_t i = 0; i < count; i++) {
        // Находим максимум
        mpz_t max_val;
        mpz_init_set_ui(max_val, 0);
        for (size_t j = 0; j < sets[i].size; j++) {
            if (mpz_cmp(sets[i].elements[j], max_val) > 0) {
                mpz_set(max_val, sets[i].elements[j]);
            }
        }

        char *max_str = mpz_get_str(NULL, 10, max_val);
        char *solution_str = serialize_mpz_set(&sets[i]);

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, (int)n);
        sqlite3_bind_text(stmt, 2, max_str, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, solution_str, -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
            success = false;
        }

        mpz_clear(max_val);
        free(max_str);
        free(solution_str);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(manager->db, "COMMIT;", NULL, NULL, NULL);

    pthread_mutex_unlock(&manager->mutex);
    return success;
}

// ============================================================================
// Функции загрузки
// ============================================================================

bool db_manager_get_result(DatabaseManager *manager, uint32_t n, SolutionResult *result) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_SELECT_RESULT, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    sqlite3_bind_int(stmt, 1, (int)n);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result->n = n;

        const char *max_str = (const char *)sqlite3_column_text(stmt, 0);
        mpz_set_str(result->max_value, max_str, 10);

        const char *solution_str = (const char *)sqlite3_column_text(stmt, 1);
        deserialize_mpz_set(solution_str, &result->solution_set);

        result->computation_time = sqlite3_column_double(stmt, 2);

        const char *status_str = (const char *)sqlite3_column_text(stmt, 3);
        if (strcmp(status_str, "OPTIMAL") == 0) {
            result->status = SOLUTION_STATUS_OPTIMAL;
        } else if (strcmp(status_str, "FEASIBLE") == 0) {
            result->status = SOLUTION_STATUS_FEASIBLE;
        } else if (strcmp(status_str, "TIMEOUT") == 0) {
            result->status = SOLUTION_STATUS_TIMEOUT;
        } else if (strcmp(status_str, "INTERRUPTED") == 0) {
            result->status = SOLUTION_STATUS_INTERRUPTED;
        } else {
            result->status = SOLUTION_STATUS_NO_SOLUTION;
        }

        result->nodes_explored = (uint64_t)sqlite3_column_int64(stmt, 4);
        result->timestamp = (time_t)sqlite3_column_int64(stmt, 5);

        found = true;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return found;
}

bool db_manager_get_best_bound(DatabaseManager *manager, uint32_t n, mpz_t bound) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_SELECT_BEST_BOUND, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    sqlite3_bind_int(stmt, 1, (int)n);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        mpz_set_si(bound, sqlite3_column_int64(stmt, 0));
        found = true;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return found;
}

bool db_manager_has_optimal_solution(DatabaseManager *manager, uint32_t n) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_HAS_OPTIMAL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    sqlite3_bind_int(stmt, 1, (int)n);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return found;
}

uint32_t db_manager_get_last_n(DatabaseManager *manager) {
    if (!manager || !manager->initialized) return 0;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_LAST_N, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        return 0;
    }

    uint32_t last_n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        last_n = (uint32_t)sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return last_n;
}

size_t db_manager_get_optimal_sets(DatabaseManager *manager, uint32_t n, MpzSet **sets) {
    if (!manager || !manager->initialized) {
        *sets = NULL;
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_SELECT_OPTIMAL_SETS, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        *sets = NULL;
        return 0;
    }

    sqlite3_bind_int(stmt, 1, (int)n);

    // Сначала считаем количество
    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }

    if (count == 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&manager->mutex);
        *sets = NULL;
        return 0;
    }

    // Выделяем память
    *sets = malloc(count * sizeof(MpzSet));
    for (size_t i = 0; i < count; i++) {
        mpz_set_init(&(*sets)[i], 16);
    }

    // Читаем данные
    sqlite3_reset(stmt);
    size_t idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        const char *solution_str = (const char *)sqlite3_column_text(stmt, 0);
        deserialize_mpz_set(solution_str, &(*sets)[idx]);
        idx++;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return count;
}

size_t db_manager_get_all_results(DatabaseManager *manager, SolutionResult **results) {
    if (!manager || !manager->initialized) {
        *results = NULL;
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_SELECT_ALL_RESULTS, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        *results = NULL;
        return 0;
    }

    // Считаем количество
    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }

    if (count == 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&manager->mutex);
        *results = NULL;
        return 0;
    }

    // Выделяем память
    *results = malloc(count * sizeof(SolutionResult));
    for (size_t i = 0; i < count; i++) {
        solution_result_init(&(*results)[i]);
    }

    // Читаем данные
    sqlite3_reset(stmt);
    size_t idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        SolutionResult *r = &(*results)[idx];

        r->n = (uint32_t)sqlite3_column_int(stmt, 0);

        const char *max_str = (const char *)sqlite3_column_text(stmt, 1);
        mpz_set_str(r->max_value, max_str, 10);

        const char *solution_str = (const char *)sqlite3_column_text(stmt, 2);
        deserialize_mpz_set(solution_str, &r->solution_set);

        r->computation_time = sqlite3_column_double(stmt, 3);

        const char *status_str = (const char *)sqlite3_column_text(stmt, 4);
        if (strcmp(status_str, "OPTIMAL") == 0) {
            r->status = SOLUTION_STATUS_OPTIMAL;
        } else if (strcmp(status_str, "FEASIBLE") == 0) {
            r->status = SOLUTION_STATUS_FEASIBLE;
        } else {
            r->status = SOLUTION_STATUS_NO_SOLUTION;
        }

        r->nodes_explored = (uint64_t)sqlite3_column_int64(stmt, 5);
        r->timestamp = sqlite3_column_int64(stmt, 6);

        idx++;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return count;
}

size_t db_manager_get_all_optimal_summary(DatabaseManager *manager, OptimalSummary **summary) {
    if (!manager || !manager->initialized) {
        *summary = NULL;
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_SELECT_SUMMARY, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        *summary = NULL;
        return 0;
    }

    // Считаем количество
    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }

    if (count == 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&manager->mutex);
        *summary = NULL;
        return 0;
    }

    // Выделяем память
    *summary = malloc(count * sizeof(OptimalSummary));

    // Читаем данные
    sqlite3_reset(stmt);
    size_t idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        OptimalSummary *s = &(*summary)[idx];

        s->n = (uint32_t)sqlite3_column_int(stmt, 0);
        s->max_value_str = strdup((const char *)sqlite3_column_text(stmt, 1));
        s->solutions_count = (size_t)sqlite3_column_int(stmt, 2);
        s->computation_time = sqlite3_column_double(stmt, 3);
        s->status = SOLUTION_STATUS_OPTIMAL;

        idx++;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return count;
}

void db_manager_free_summary(OptimalSummary *summary, size_t count) {
    if (!summary) return;
    for (size_t i = 0; i < count; i++) {
        free(summary[i].max_value_str);
    }
    free(summary);
}

bool db_manager_get_stats(DatabaseManager *manager, DatabaseStats *stats) {
    if (!manager || !manager->initialized) return false;

    pthread_mutex_lock(&manager->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(manager->db, SQL_GET_STATS, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats->total_results = (size_t)sqlite3_column_int(stmt, 0);
        stats->optimal_results = (size_t)sqlite3_column_int(stmt, 1);
        stats->max_n_solved = sqlite3_column_type(stmt, 2) != SQLITE_NULL ?
                              (uint32_t)sqlite3_column_int(stmt, 2) : 0;
        stats->total_computation_time = sqlite3_column_double(stmt, 3);
        found = true;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&manager->mutex);

    return found;
}

// ============================================================================
// Функции вывода
// ============================================================================

void db_manager_print_result(DatabaseManager *manager, uint32_t n) {
    SolutionResult result;
    solution_result_init(&result);

    if (db_manager_get_result(manager, n, &result)) {
        char *max_str = mpz_get_str(NULL, 10, result.max_value);
        char *set_str = mpz_set_to_string(&result.solution_set);

        printf("N=%u:\n", n);
        printf("  Максимум: %s\n", max_str);
        printf("  Множество: %s\n", set_str);
        printf("  Время: %.2f сек\n", result.computation_time);
        printf("  Узлов: %lu\n", result.nodes_explored);
        printf("  Статус: %s\n", solution_status_to_string(result.status));

        free(max_str);
        free(set_str);
    } else {
        printf("Результат для N=%u не найден\n", n);
    }

    solution_result_clear(&result);
}

void db_manager_print_all_results(DatabaseManager *manager) {
    OptimalSummary *summary;
    size_t count = db_manager_get_all_optimal_summary(manager, &summary);

    if (count == 0) {
        printf("Нет сохраненных результатов\n");
        return;
    }

    printf("%-5s %-15s %-10s %-15s\n", "N", "Max", "Решений", "Время (сек)");
    printf("%-5s %-15s %-10s %-15s\n", "-----", "---------------",
           "----------", "---------------");

    for (size_t i = 0; i < count; i++) {
        printf("%-5u %-15s %-10zu %-15.2f\n",
               summary[i].n,
               summary[i].max_value_str,
               summary[i].solutions_count,
               summary[i].computation_time);
    }

    db_manager_free_summary(summary, count);
}
