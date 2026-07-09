/**
 * @file    bench_bignum_div_bignum.c
 * @brief   Микробенчмарк для профилирования bignum_div_bignum (ST).
 * @author  git@bayborodov.com
 * @version 1.0.1
 * @date    08.07.2026
 *
 * @details
 *   ST-бенчмарк для деления bignum на bignum.
 *   - splitmix64 PRNG без блокировок.
 *   - Делимое (n) и делитель (d) генерируются случайно (bignum_t).
 *   - Исключены тривиальные деления (d > n).
 *   - Anti-DCE барьер через ассемблерную вставку.
 *   - Проверка корректности (N = Q * D + R) перед профилированием.
 *   - ITERATIONS = 100M с прогресс-баром.
 *
 * # Сборка
 *  gcc -O2 -I include -I libs/bignum-common/include -no-pie -fno-omit-frame-pointer \
 *    benchmarks/bench_bignum_div_bignum.c build/bignum_div_bignum.o \
 *    -o bin/bench_bignum_div_bignum
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include <bignum.h>
#include "bignum_div_bignum.h"

#define BIGNUM_CAPACITY 32
#define BIGNUM_BITS (BIGNUM_CAPACITY * 64)

#define ITERATIONS 1000000u
#define PREGEN_DATA_COUNT 8192

/* --- splitmix64: детерминированный, без блокировок, 64-bit состояние --- */
static uint64_t splitmix_state = 0x9E3779B97F4A7C15ULL;

static inline uint64_t splitmix64_next(void) {
    uint64_t z = (splitmix_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Заполняет bignum случайными словами и устанавливает len ∈ [1, CAPACITY].
 * Ведущее слово гарантированно != 0 (нормализация по контракту bignum_t). */
static void init_random_bignum(bignum_t *num) {
    int used = (int)((splitmix64_next() & 0x1F)) + 1;  /* [1, 32] */
    if (used > BIGNUM_CAPACITY) used = BIGNUM_CAPACITY;
    num->len = (size_t)used;
    for (int i = 0; i < used; ++i) {
        num->words[i] = splitmix64_next();
    }
    /* Старшее слово != 0 по контракту — гарантируем: */
    num->words[used - 1] |= 1ULL;
}

/* Вспомогательная функция для проверки корректности: N == Q * D + R */
static bool verify_division(const bignum_t *n, const bignum_t *d, const bignum_t *q, const bignum_t *rem) {
    uint64_t calc[BIGNUM_CAPACITY] = {0};
    
    /* 1. Умножение: calc = Q * D */
    for (size_t i = 0; i < q->len; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < d->len; ++j) {
            if (i + j >= BIGNUM_CAPACITY) break;
            __extension__ unsigned __int128 prod = (unsigned __int128)q->words[i] * d->words[j] + calc[i+j] + carry;

            //unsigned __int128 prod = (unsigned __int128)q->words[i] * d->words[j] + calc[i+j] + carry;
            calc[i+j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        if (i + d->len < BIGNUM_CAPACITY) {
            calc[i + d->len] += carry;
        }
    }
    
    /* 2. Сложение: calc = calc + R */
    uint64_t carry = 0;
    for (size_t i = 0; i < BIGNUM_CAPACITY; ++i) {
        uint64_t r_word = (i < rem->len) ? rem->words[i] : 0;
        __extension__ unsigned __int128 sum = (unsigned __int128)calc[i] + r_word + carry;
        calc[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    
    /* 3. Сравнение: calc == N */
    for (size_t i = 0; i < BIGNUM_CAPACITY; ++i) {
        uint64_t n_word = (i < n->len) ? n->words[i] : 0;
        if (calc[i] != n_word) return false;
    }
    
    return true;
}

int main(void) {
    /* --- Фаза 1: Предварительная генерация данных --- */
    printf("Pregenerating %u data sets (splitmix64)...\n", PREGEN_DATA_COUNT);

    /* Округление размера до кратного 64 для корректной работы aligned_alloc в C11 */
    size_t alloc_size = (sizeof(bignum_t) * PREGEN_DATA_COUNT + 63) & ~63;
    bignum_t *n_sources = aligned_alloc(64, alloc_size);
    bignum_t *d_sources = aligned_alloc(64, alloc_size);
    
    /* Инициализация выходных буферов нулями */
    bignum_t q_buf = {0};
    bignum_t rem_buf = {0};

    if (!n_sources || !d_sources) {
        perror("Failed to allocate memory for test data");
        return 1;
    }

    /* seed splitmix64 на основе времени */
    splitmix_state = (uint64_t)time(NULL) ^ 0x9E3779B97F4A7C15ULL;

    for (unsigned i = 0; i < PREGEN_DATA_COUNT; ++i) {
        init_random_bignum(&n_sources[i]);
        init_random_bignum(&d_sources[i]);
        
        /* Исключаем тривиальное деление (когда делитель больше делимого) */
        if (d_sources[i].len > n_sources[i].len) {
            d_sources[i].len = (splitmix64_next() % n_sources[i].len) + 1;
            d_sources[i].words[d_sources[i].len - 1] |= 1ULL; /* нормализация */
        }
    }

    /* --- Фаза 2: Проверка корректности алгоритма --- */
    printf("Verifying division correctness (N = Q * D + R)...\n");
    bignum_div_bignum(&n_sources[0], &d_sources[0], &q_buf, &rem_buf);
    if (!verify_division(&n_sources[0], &d_sources[0], &q_buf, &rem_buf)) {
        fprintf(stderr, "Assertion failed: Division result is incorrect!\n");
        free(n_sources);
        free(d_sources);
        return 1;
    }
    printf("Verification passed successfully!\n\n");

    /* --- Фаза 3: "Горячий" цикл для профилирования --- */
    printf("Starting benchmark with %u iterations...\n", ITERATIONS);

    uint32_t progress_step = (ITERATIONS >= 100) ? (ITERATIONS / 100) : 1;
    uint32_t next_progress = progress_step;
    int percent = 0;

    for (uint32_t i = 0; i < ITERATIONS; ++i) {
        unsigned data_idx = i & (PREGEN_DATA_COUNT - 1);  /* степень 2 — быстрее % */
        bignum_div_bignum(&n_sources[data_idx], &d_sources[data_idx], &q_buf, &rem_buf);
        
        /* Надежный Anti-DCE барьер для GCC/Clang: 
         * Указываем компилятору, что память q_buf и rem_buf изменена */
        __asm__ volatile("" : : "r"(&q_buf), "r"(&rem_buf) : "memory");

        /* Отрисовка прогресс-бара (без использования % в горячем цикле) */
        if (i + 1 == next_progress) {
            percent++;
            int pos = percent / 2;
            char bar[51];
            for (int b = 0; b < 50; ++b) {
                if (b < pos) bar[b] = '=';
                else if (b == pos && percent < 100) bar[b] = '>';
                else bar[b] = ' ';
            }
            bar[50] = '\0';
            printf("\r[%s] %d%%", bar, percent);
            fflush(stdout);
            next_progress += progress_step;
        }
    }

    printf("\nBenchmark finished. last q_len = %zu, rem_len = %zu\n",
           q_buf.len, rem_buf.len);

    free(n_sources);
    free(d_sources);
    return 0;
}
