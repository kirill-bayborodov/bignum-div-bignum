/**
 * @file    bignum_div_bignum.c
 * @brief   Эталонная реализация деления больших чисел (Knuth Algorithm D).
 * @details Выполняет пословное деление по основанию 2^64.
 */

#include "bignum_div_bignum.h"
#include <string.h>
#include <stdbool.h>

/* Вспомогательная функция для проверки перекрытия буферов в памяти */
static inline bool check_overlap(const void *a, const void *b, size_t size) {
    const char *p1 = (const char *)a;
    const char *p2 = (const char *)b;
    return (p1 < p2 + size) && (p2 < p1 + size);
}

/* Вспомогательная функция для подсчета ведущих нулей в 64-битном слове */
static inline int count_leading_zeros(uint64_t x) {
    if (x == 0) return 64;
    return __builtin_clzll(x);
}

/* Нормализация: вычисление реальной длины числа (без ведущих нулей) */
static inline size_t get_real_len(const bignum_t *b) {
    size_t len = b->len;
    while (len > 0 && b->words[len - 1] == 0) {
        len--;
    }
    return len;
}

bignum_div_bignum_status_t bignum_div_bignum(const bignum_t *dividend,
                                             const bignum_t *divisor,
                                             bignum_t *quotient,
                                             bignum_t *remainder) {
    /* 1. Проверка на NULL */
    if (!dividend || !divisor || !quotient || !remainder) {
        return BIGNUM_DIV_BIGNUM_ERR_NULL_PTR;
    }

    /* 2. Проверка длины (не больше BIGNUM_CAPACITY) */
    if (dividend->len > BIGNUM_CAPACITY || divisor->len > BIGNUM_CAPACITY) {
        return BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH;
    }

    /* 3. Проверка перекрытия буферов (Memory Guard) */
    size_t bn_size = sizeof(bignum_t);
    if (check_overlap(quotient, dividend, bn_size) ||
        check_overlap(quotient, divisor, bn_size) ||
        check_overlap(remainder, dividend, bn_size) ||
        check_overlap(remainder, divisor, bn_size) ||
        check_overlap(quotient, remainder, bn_size)) {
        return BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP;
    }

    /* 4. Определение реальных длин (игнорируя ведущие нули) */
    size_t m = get_real_len(dividend);
    size_t n = get_real_len(divisor);

    /* 5. Проверка деления на ноль */
    if (n == 0) {
        return BIGNUM_DIV_BIGNUM_ERR_DIVISION_BY_ZERO;
    }

    /* Инициализация результатов нулями */
    memset(quotient, 0, sizeof(bignum_t));
    memset(remainder, 0, sizeof(bignum_t));

    /* 6. Если делимое меньше делителя: Q = 0, R = dividend */
    bool is_less = false;
    if (m < n) {
        is_less = true;
    } else if (m == n) {
        for (int i = m - 1; i >= 0; i--) {
            if (dividend->words[i] < divisor->words[i]) {
                is_less = true;
                break;
            } else if (dividend->words[i] > divisor->words[i]) {
                break;
            }
        }
    }

    if (is_less) {
        memcpy(remainder->words, dividend->words, m * sizeof(uint64_t));
        remainder->len = m;
        return BIGNUM_DIV_BIGNUM_OK;
    }

    /* 7. Быстрый путь: делитель состоит из 1 слова */
    if (n == 1) {
        uint64_t div_word = divisor->words[0];
        uint64_t rem_word = 0;
        
        for (int i = m - 1; i >= 0; i--) {
            __extension__ unsigned __int128 num = ((unsigned __int128)rem_word << 64) | dividend->words[i];
            quotient->words[i] = (uint64_t)(num / div_word);
            rem_word = (uint64_t)(num % div_word);
        }
        
        remainder->words[0] = rem_word;
        remainder->len = (rem_word > 0) ? 1 : 0;
        
        size_t q_len = m;
        while (q_len > 0 && quotient->words[q_len - 1] == 0) q_len--;
        quotient->len = q_len;
        
        return BIGNUM_DIV_BIGNUM_OK;
    }

    /* 8. Алгоритм Кнута D для многословного делителя */
    
    /* Массивы для нормализованных чисел. 
       u (делимое) требует m + 1 слов для обработки переполнения старшего бита. */
    uint64_t u[BIGNUM_CAPACITY + 1] = {0};
    uint64_t v[BIGNUM_CAPACITY] = {0};
    uint64_t q[BIGNUM_CAPACITY] = {0};

    /* Шаг D1: Нормализация. Сдвигаем влево, чтобы старший бит V[n-1] стал 1 */
    int shift = count_leading_zeros(divisor->words[n - 1]);
    
    if (shift > 0) {
        /* Сдвиг делителя */
        for (size_t i = n - 1; i > 0; i--) {
            v[i] = (divisor->words[i] << shift) | (divisor->words[i - 1] >> (64 - shift));
        }
        v[0] = divisor->words[0] << shift;

        /* Сдвиг делимого */
        uint64_t carry = 0;
        for (size_t i = 0; i < m; i++) {
            u[i] = (dividend->words[i] << shift) | carry;
            carry = dividend->words[i] >> (64 - shift);
        }
        u[m] = carry; /* Дополнительное слово для переполнения */
    } else {
        memcpy(v, divisor->words, n * sizeof(uint64_t));
        memcpy(u, dividend->words, m * sizeof(uint64_t));
        u[m] = 0;
    }

    /* Шаг D2-D7: Основной цикл деления */
    for (int j = m - n; j >= 0; j--) {
        /* Шаг D3: Вычисление пробного частного q_hat */
        __extension__ unsigned __int128 u_top = ((unsigned __int128)u[j + n] << 64) | u[j + n - 1];
        uint64_t v_top = v[n - 1];
        uint64_t q_hat;
        uint64_t r_hat;

        if (u[j + n] == v_top) {
            q_hat = UINT64_MAX;
            r_hat = u[j + n - 1] + v_top;
            /* Если r_hat переполнился, q_hat корректировать не нужно */
            if (r_hat < v_top) goto multiply_subtract; 
        } else {
            q_hat = (uint64_t)(u_top / v_top);
            r_hat = (uint64_t)(u_top % v_top);
        }

        /* Корректировка q_hat */
        while (1) {
            __extension__ unsigned __int128 p = (unsigned __int128)q_hat * v[n - 2];
            __extension__ unsigned __int128 r_cmp = ((unsigned __int128)r_hat << 64) | u[j + n - 2];
            if (p <= r_cmp) break;
            
            q_hat--;
            r_hat += v_top;
            if (r_hat < v_top) break; /* Переполнение r_hat означает, что r_cmp > p */
        }

    multiply_subtract: ;
        /* Шаг D4: Умножение и вычитание (U[j..j+n] -= q_hat * V) */
        uint64_t borrow = 0;
        for (size_t i = 0; i < n; i++) {
            __extension__ unsigned __int128 p = (unsigned __int128)q_hat * v[i];
            uint64_t p_low = (uint64_t)p;
            uint64_t p_high = (uint64_t)(p >> 64);

            uint64_t sub1 = u[j + i] - borrow;
            uint64_t b1 = (u[j + i] < borrow) ? 1 : 0;

            uint64_t sub2 = sub1 - p_low;
            uint64_t b2 = (sub1 < p_low) ? 1 : 0;

            u[j + i] = sub2;
            borrow = p_high + b1 + b2;
        }
        
        uint64_t sub_top = u[j + n] - borrow;
        uint64_t b_top = (u[j + n] < borrow) ? 1 : 0;
        u[j + n] = sub_top;

        /* Шаг D5: Проверка остатка. Шаг D6: Компенсация (Add Back) */
        if (b_top) {
            q_hat--;
            uint64_t carry2 = 0;
            for (size_t i = 0; i < n; i++) {
                __extension__ unsigned __int128 sum = (unsigned __int128)u[j + i] + v[i] + carry2;
                u[j + i] = (uint64_t)sum;
                carry2 = (uint64_t)(sum >> 64);
            }
            u[j + n] += carry2;
        }

        q[j] = q_hat;
    }

    /* Шаг D8: Денормализация остатка (сдвиг вправо на shift) */
    if (shift > 0) {
        for (size_t i = 0; i < n - 1; i++) {
            remainder->words[i] = (u[i] >> shift) | (u[i + 1] << (64 - shift));
        }
        remainder->words[n - 1] = u[n - 1] >> shift;
    } else {
        memcpy(remainder->words, u, n * sizeof(uint64_t));
    }

    /* Копируем частное */
    memcpy(quotient->words, q, (m - n + 1) * sizeof(uint64_t));

    /* Устанавливаем корректные длины (убираем ведущие нули) */
    size_t q_len = m - n + 1;
    while (q_len > 0 && quotient->words[q_len - 1] == 0) {
        q_len--;
    }
    quotient->len = q_len;

    size_t r_len = n;
    while (r_len > 0 && remainder->words[r_len - 1] == 0) {
        r_len--;
    }
    remainder->len = r_len;

    return BIGNUM_DIV_BIGNUM_OK;
}
