/**
 * @file    bench_bignum_template_mt.c
 * @brief   Многопоточный микробенчмарк для профилирования bignum_template.
 * @author  git@bayborodov.com
 * @version 1.0.0
 * @date    03.10.2025
 *
 * @details
 *   Для чистоты измерений все случайные данные генерируются заранее
 *   в основном потоке и передаются в рабочие потоки. Каждый поток
 *   выполняет свой набор вызовов bignum_template, используя
 *   общий пул предварительно сгенерированных данных.
 *
 * @history
 *   - rev 1.0 (12.08.2025): Первоначальная версия.
 *   - rev 1.1 (13.08.2025): Реализована предварительная генерация данных
 *                           в main для исключения rand() из потоков.
 *
 * # Сборка
 * gcc -g -O2 -I include -no-pie -fno-omit-frame-pointer -pthread \
 *   benchmarks/bench_bignum_template_mt.c build/bignum_template.o \
 *   -o bin/bench_bignum_template_mt
 *
 * # Запуск perf
 * /usr/local/bin/perf record -F 9999 -o benchmarks/reports/report_bench_bignum_template_mt -g -- \
 *   bin/bench_bignum_template_mt
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <bignum.h>
#include "bignum_template.h"

// --- Локальные определения для компиляции ---
#define BIGNUM_CAPACITY 32
#define BIGNUM_BITS (BIGNUM_CAPACITY * 64)

#ifndef ITER_PER_THREAD
#  define ITER_PER_THREAD (20000000u * 20)
#endif

#ifndef THREAD_COUNT
#  define THREAD_COUNT 4
#endif

#define PREGEN_DATA_COUNT 8192
#define MAX_SHIFT (BIGNUM_BITS - 1)

// Структура для передачи данных в поток
typedef struct {
    unsigned thread_id;
    unsigned iters;
    const bignum_t* sources; // Указатель на общий пул исходных чисел
    const size_t* shifts;    // Указатель на общий пул сдвигов
    unsigned data_count;     // Размер пула
} thread_arg_t;

/** Инициализация случайного bignum_t */
static void init_random_bignum(bignum_t *num) {
    int used = (rand() % BIGNUM_CAPACITY) + 1;
    num->len = used;
    for (int i = 0; i < used; ++i) {
        num->words[i] = ((uint64_t)rand() << 32) | rand();
    }
    for (int i = used; i < BIGNUM_CAPACITY; ++i) {
        num->words[i] = 0;
    }
}

/** Функция, исполняемая каждым потоком */
static void* thread_func(void *arg) {
    const thread_arg_t *t = arg;

    for (unsigned i = 0; i < t->iters; ++i) {
        // Используем общий пул данных, циклически
        // Смещаем индекс на thread_id, чтобы потоки реже работали с одними и теми же данными
        unsigned data_idx = (i + t->thread_id) % t->data_count;

        bignum_t dst = t->sources[data_idx];
        size_t shift = t->shifts[data_idx];

        bignum_template(&dst, shift);

        if (dst.len == 0xDEADBEEF) {
            return (void*)1;
        }
    }
    return NULL;
}

int main(void) {
    // --- Фаза 1: Предварительная генерация данных в основном потоке ---
    printf("Pregenerating %u data sets for %u threads...\n", PREGEN_DATA_COUNT, THREAD_COUNT);
    bignum_t* sources = malloc(sizeof(bignum_t) * PREGEN_DATA_COUNT);
    size_t* shifts = malloc(sizeof(size_t) * PREGEN_DATA_COUNT);

    if (!sources || !shifts) {
        perror("Failed to allocate memory for test data");
        return 1;
    }

    srand((unsigned)time(NULL));
    for (unsigned i = 0; i < PREGEN_DATA_COUNT; ++i) {
        init_random_bignum(&sources[i]);
        shifts[i] = (size_t)(rand() % MAX_SHIFT);
    }

    // --- Фаза 2: Запуск потоков и профилирование ---
    printf("Starting benchmark with %u threads, %u iterations each...\n", THREAD_COUNT, ITER_PER_THREAD);
    pthread_t threads[THREAD_COUNT];
    thread_arg_t args[THREAD_COUNT];

    for (unsigned i = 0; i < THREAD_COUNT; ++i) {
        args[i].thread_id = i;
        args[i].iters     = ITER_PER_THREAD;
        args[i].sources   = sources;
        args[i].shifts    = shifts;
        args[i].data_count = PREGEN_DATA_COUNT;
        if (pthread_create(&threads[i], NULL, thread_func, &args[i]) != 0) {
            perror("pthread_create");
            free(sources);
            free(shifts);
            return 1;
        }
    }

    for (unsigned i = 0; i < THREAD_COUNT; ++i) {
        void *res;
        pthread_join(threads[i], &res);
        if (res != NULL) {
            fprintf(stderr, "Error in thread %u\n", i);
        }
    }
    
    printf("Benchmark finished.\n");

    // --- Фаза 3: Очистка ---
    free(sources);
    free(shifts);

    return 0;
}
