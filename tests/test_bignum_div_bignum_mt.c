/**
 * @file    test_bignum_div_bignum_mt.c
 * @author  git@bayborodov.com
 * @version 1.0.0
 * @date    08.07.2026
 *
 * @brief   Тест на потокобезопасность для модуля bignum_div_bignum.
 *
 * @details
 *   Тест создает несколько потоков, каждый из которых многократно
 *   выполняет операцию деления над своими собственными, уникальными
 *   экземплярами `bignum_t`. После завершения всех потоков проверяется,
 *   что конечный результат соответствует ожидаемому.
 */

#include "bignum_div_bignum.h"
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>

#define NUM_THREADS 8
#define NUM_ITERATIONS 10000

typedef struct {
    bignum_t dividend;
    bignum_t divisor;
    bignum_t quotient;
    bignum_t remainder;
    bignum_t expected_q;
    bignum_t expected_r;
    int thread_id;
} thread_data_t;

// Вспомогательная функция для сравнения
static int bignum_are_equal(const bignum_t* a, const bignum_t* b) {
    if (a->len != b->len) return 0;
    if (a->len == 0) return 1;
    return memcmp(a->words, b->words, a->len * sizeof(uint64_t)) == 0;
}

void* worker_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        bignum_div_bignum(&data->dividend, &data->divisor, &data->quotient, &data->remainder);
        // Слегка модифицируем делимое на каждой итерации, чтобы вычисления менялись
        data->dividend.words[0] += 1;
    }

    return NULL;
}

int main() {
    printf("--- Starting MT test for bignum_div_bignum ---\n");

    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_data[i].thread_id = i;
        
        memset(&thread_data[i].dividend, 0, sizeof(bignum_t));
        memset(&thread_data[i].divisor, 0, sizeof(bignum_t));

        // Уникальное начальное значение для каждого потока
        thread_data[i].dividend.len = 1;
        thread_data[i].dividend.words[0] = 1000000 + i;

        thread_data[i].divisor.len = 1;
        thread_data[i].divisor.words[0] = 7;

        // Рассчитываем ожидаемый результат так же, как это делает поток
        bignum_t temp_dividend = thread_data[i].dividend;
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
            bignum_div_bignum(&temp_dividend, &thread_data[i].divisor, 
                              &thread_data[i].expected_q, &thread_data[i].expected_r);
            temp_dividend.words[0] += 1;
        }

        pthread_create(&threads[i], NULL, worker_thread, &thread_data[i]);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    // Проверка результатов
    for (int i = 0; i < NUM_THREADS; ++i) {
        assert(bignum_are_equal(&thread_data[i].quotient, &thread_data[i].expected_q));
        assert(bignum_are_equal(&thread_data[i].remainder, &thread_data[i].expected_r));
    }

    printf("--- MT test for bignum_div_bignum passed ---\n");
    return 0;
}
