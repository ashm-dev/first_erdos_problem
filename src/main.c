/**
 * main.c - Точка входа Erdos Solver
 *
 * CLI интерфейс и параллельное выполнение.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>

#include "../include/types.h"
#include "../include/logger.h"
#include "../include/subset_sum_manager.h"
#include "../include/backtrack_solver.h"
#include "../include/db_manager.h"

// ============================================================================
// Глобальные переменные
// ============================================================================

static volatile bool g_stop_flag = false;
static pthread_mutex_t g_result_mutex = PTHREAD_MUTEX_INITIALIZER;
static DatabaseManager *g_db_manager = NULL;

// ============================================================================
// Структуры для параллельного выполнения
// ============================================================================

typedef struct {
    uint32_t n;
    bool find_all_optimal;
    bool first_only;
    const char *db_path;
    volatile bool *stop_flag;
} WorkerTask;

typedef struct {
    pthread_t thread;
    WorkerTask task;
    SolutionResult result;
    bool completed;
} Worker;

// ============================================================================
// Обработчик сигналов
// ============================================================================

static void signal_handler(int sig) {
    (void)sig;
    LOG_WARNING("Получен сигнал прерывания, останавливаем вычисления...");
    g_stop_flag = true;
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// ============================================================================
// Функция воркера
// ============================================================================

static void* worker_thread(void *arg) {
    Worker *worker = (Worker *)arg;
    WorkerTask *task = &worker->task;

    // Инициализируем результат
    solution_result_init(&worker->result);

    // Проверяем, решено ли уже
    if (g_db_manager && db_manager_has_optimal_solution(g_db_manager, task->n)) {
        LOG_INFO("N=%u уже решено, пропускаем", task->n);
        worker->result.n = task->n;
        worker->result.status = SOLUTION_STATUS_OPTIMAL;
        worker->completed = true;
        return NULL;
    }

    // Выбираем тип менеджера
    ManagerType manager_type = task->n < 25 ? MANAGER_TYPE_FAST : MANAGER_TYPE_ITERATIVE;

    // Создаем конфиг
    SolverConfig config = {
        .n = task->n,
        .find_all_optimal = task->find_all_optimal,
        .first_only = task->first_only,
        .manager_type = manager_type,
        .log_interval_sec = ERDOS_LOG_INTERVAL_SEC,
        .stop_flag = task->stop_flag,
        .initial_bound = 0
    };

    // Пробуем получить границу из БД
    if (g_db_manager) {
        value_t bound;
        if (db_manager_get_best_bound(g_db_manager, task->n, &bound)) {
            config.initial_bound = bound;
            LOG_INFO("N=%u: используем границу из БД", task->n);
        }
    }

    // Создаем и запускаем решатель
    BacktrackSolver *solver = backtrack_solver_create(&config);

    if (task->find_all_optimal) {
        backtrack_solver_solve_all(solver, &worker->result);
    } else {
        backtrack_solver_solve(solver, &worker->result);
    }

    // Сохраняем результат в БД
    if (g_db_manager && worker->result.status == SOLUTION_STATUS_OPTIMAL) {
        pthread_mutex_lock(&g_result_mutex);
        db_manager_save_result(g_db_manager, &worker->result);

        // Сохраняем все оптимальные решения если нужно
        if (task->find_all_optimal) {
            NumberSet *optimal_sets;
            size_t count = backtrack_solver_get_optimal_solutions(solver, &optimal_sets);
            if (count > 0) {
                db_manager_save_optimal_sets(g_db_manager, task->n, optimal_sets, count);
            }
        }
        pthread_mutex_unlock(&g_result_mutex);
    }

    backtrack_solver_destroy(solver);

    worker->completed = true;
    return NULL;
}

// ============================================================================
// Функции запуска
// ============================================================================

static void run_single(uint32_t n, bool find_all, bool first_only, const char *db_path) {
    LOG_INFO("Запуск решения для N=%u", n);

    g_db_manager = db_manager_create(db_path);

    Worker worker = {0};
    worker.task.n = n;
    worker.task.find_all_optimal = find_all;
    worker.task.first_only = first_only;
    worker.task.db_path = db_path;
    worker.task.stop_flag = &g_stop_flag;
    worker.completed = false;

    pthread_create(&worker.thread, NULL, worker_thread, &worker);
    pthread_join(worker.thread, NULL);

    solution_result_clear(&worker.result);
    db_manager_destroy(g_db_manager);
    g_db_manager = NULL;
}

static void run_range(uint32_t start_n, uint32_t max_n, uint32_t num_workers,
                      bool find_all, bool first_only, const char *db_path) {
    LOG_INFO("Запуск параллельного решения: N=%u..%u, воркеров=%u",
             start_n, max_n, num_workers);

    g_db_manager = db_manager_create(db_path);

    // Определяем начальный N
    uint32_t last_n = db_manager_get_last_n(g_db_manager);
    if (start_n == 0) {
        start_n = last_n > 0 ? last_n + 1 : 1;
    }

    LOG_INFO("Начинаем с N=%u", start_n);

    // Создаем воркеров
    Worker *workers = calloc(num_workers, sizeof(Worker));

    uint32_t current_n = start_n;
    uint32_t active_workers = 0;

    while (!g_stop_flag && (current_n <= max_n || active_workers > 0)) {
        // Запускаем новые воркеры если есть место
        for (uint32_t i = 0; i < num_workers && current_n <= max_n && !g_stop_flag; i++) {
            if (!workers[i].completed && workers[i].task.n == 0) {
                // Свободный слот - запускаем нового воркера
                workers[i].task.n = current_n;
                workers[i].task.find_all_optimal = find_all;
                workers[i].task.first_only = first_only;
                workers[i].task.db_path = db_path;
                workers[i].task.stop_flag = &g_stop_flag;
                workers[i].completed = false;

                pthread_create(&workers[i].thread, NULL, worker_thread, &workers[i]);
                active_workers++;
                current_n++;
            }
        }

        // Проверяем завершенные воркеры
        for (uint32_t i = 0; i < num_workers; i++) {
            if (workers[i].task.n != 0 && workers[i].completed) {
                pthread_join(workers[i].thread, NULL);
                solution_result_clear(&workers[i].result);

                // Сбрасываем слот
                workers[i].task.n = 0;
                workers[i].completed = false;
                active_workers--;
            }
        }

        // Небольшая пауза чтобы не грузить CPU
        usleep(100000);  // 100ms
    }

    // Ждем завершения всех воркеров
    for (uint32_t i = 0; i < num_workers; i++) {
        if (workers[i].task.n != 0) {
            pthread_join(workers[i].thread, NULL);
            solution_result_clear(&workers[i].result);
        }
    }

    free(workers);
    db_manager_destroy(g_db_manager);
    g_db_manager = NULL;

    if (g_stop_flag) {
        LOG_WARNING("Вычисления прерваны пользователем");
    } else {
        LOG_INFO("Все вычисления завершены");
    }
}

// ============================================================================
// Вывод справки
// ============================================================================

static void print_usage(const char *prog_name) {
    printf("Erdos Solver - Поиск B-последовательностей\n\n");
    printf("Использование: %s [ОПЦИИ]\n\n", prog_name);
    printf("Опции:\n");
    printf("  -n, --n N            Решить для конкретного N\n");
    printf("  -s, --start-n N      Начать с N (по умолчанию: продолжить)\n");
    printf("  -m, --max-n N        Максимальное N (по умолчанию: без ограничений)\n");
    printf("  -w, --workers N      Количество параллельных воркеров (по умолчанию: 1)\n");
    printf("  -d, --db PATH        Путь к базе данных (по умолчанию: %s)\n", ERDOS_DEFAULT_DB_PATH);
    printf("  -a, --all            Искать все оптимальные решения\n");
    printf("  -f, --first-only     Остановиться на первом решении\n");
    printf("  --show [N]           Показать результаты (для N или все)\n");
    printf("  --stats              Показать статистику БД\n");
    printf("  -v, --verbose        Подробный вывод\n");
    printf("  -h, --help           Показать эту справку\n");
    printf("\nПримеры:\n");
    printf("  %s -n 5              # Решить для N=5\n", prog_name);
    printf("  %s -s 1 -m 10 -w 4   # Решить N=1..10 в 4 потока\n", prog_name);
    printf("  %s --show            # Показать все результаты\n", prog_name);
    printf("  %s --show 5          # Показать результат для N=5\n", prog_name);
}

// ============================================================================
// Парсинг аргументов
// ============================================================================

typedef struct {
    uint32_t n;
    uint32_t start_n;
    uint32_t max_n;
    uint32_t workers;
    char *db_path;
    bool find_all;
    bool first_only;
    bool show_results;
    uint32_t show_n;
    bool show_stats;
    bool verbose;
    bool help;
} CliOptions;

static void parse_args(int argc, char *argv[], CliOptions *opts) {
    static struct option long_options[] = {
        {"n",          required_argument, 0, 'n'},
        {"start-n",    required_argument, 0, 's'},
        {"max-n",      required_argument, 0, 'm'},
        {"workers",    required_argument, 0, 'w'},
        {"db",         required_argument, 0, 'd'},
        {"all",        no_argument,       0, 'a'},
        {"first-only", no_argument,       0, 'f'},
        {"show",       optional_argument, 0, 'S'},
        {"stats",      no_argument,       0, 'T'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    // Значения по умолчанию
    memset(opts, 0, sizeof(CliOptions));
    opts->workers = 1;
    opts->max_n = UINT32_MAX;

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "n:s:m:w:d:afvh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'n':
                opts->n = (uint32_t)atoi(optarg);
                break;
            case 's':
                opts->start_n = (uint32_t)atoi(optarg);
                break;
            case 'm':
                opts->max_n = (uint32_t)atoi(optarg);
                break;
            case 'w':
                opts->workers = (uint32_t)atoi(optarg);
                if (opts->workers == 0) opts->workers = 1;
                break;
            case 'd':
                opts->db_path = strdup(optarg);
                break;
            case 'a':
                opts->find_all = true;
                break;
            case 'f':
                opts->first_only = true;
                break;
            case 'S':
                opts->show_results = true;
                if (optarg) {
                    opts->show_n = (uint32_t)atoi(optarg);
                }
                break;
            case 'T':
                opts->show_stats = true;
                break;
            case 'v':
                opts->verbose = true;
                break;
            case 'h':
                opts->help = true;
                break;
            default:
                break;
        }
    }

    // Проверяем остаточные аргументы для --show
    if (opts->show_results && opts->show_n == 0 && optind < argc) {
        opts->show_n = (uint32_t)atoi(argv[optind]);
    }

    if (!opts->db_path) {
        opts->db_path = strdup(ERDOS_DEFAULT_DB_PATH);
    }
}

// ============================================================================
// Главная функция
// ============================================================================

int main(int argc, char *argv[]) {
    CliOptions opts;
    parse_args(argc, argv, &opts);

    // Инициализация логгера
    logger_init(opts.verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO, NULL);

    // Справка
    if (opts.help) {
        print_usage(argv[0]);
        free(opts.db_path);
        return 0;
    }

    // Показать результаты
    if (opts.show_results) {
        DatabaseManager *db = db_manager_create(opts.db_path);
        if (db) {
            if (opts.show_n > 0) {
                db_manager_print_result(db, opts.show_n);
            } else {
                db_manager_print_all_results(db);
            }
            db_manager_destroy(db);
        }
        free(opts.db_path);
        return 0;
    }

    // Показать статистику
    if (opts.show_stats) {
        DatabaseManager *db = db_manager_create(opts.db_path);
        if (db) {
            DatabaseStats stats;
            if (db_manager_get_stats(db, &stats)) {
                printf("Статистика базы данных:\n");
                printf("  Всего результатов: %zu\n", stats.total_results);
                printf("  Оптимальных решений: %zu\n", stats.optimal_results);
                printf("  Максимальный N: %u\n", stats.max_n_solved);
                printf("  Общее время вычислений: %.2f сек\n", stats.total_computation_time);
            }
            db_manager_destroy(db);
        }
        free(opts.db_path);
        return 0;
    }

    // Установка обработчиков сигналов
    setup_signal_handlers();

    // Запуск вычислений
    if (opts.n > 0) {
        // Решение для конкретного N
        run_single(opts.n, opts.find_all, opts.first_only, opts.db_path);
    } else {
        // Параллельное решение диапазона
        run_range(opts.start_n, opts.max_n, opts.workers,
                  opts.find_all, opts.first_only, opts.db_path);
    }

    // Очистка
    free(opts.db_path);
    logger_cleanup();

    return g_stop_flag ? 1 : 0;
}
