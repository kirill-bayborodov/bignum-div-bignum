/**
 * @file    test_bignum_div_bignum.c
 * @author  git@bayborodov.com
 * @version 0.0.2
 * @date    08.07.2026
 *
 * @brief   Детерминированные тесты для модуля bignum_div_bignum.
 *
 * @details
 *   Проверяются базовая функциональность, «счастливые пути» и обработка
 *   всех ошибок, в том числе проверка длины входных чисел и перекрытие буферов.
 *
 *   Для компиляции (пример):
 *   gcc -g -Wall -Wextra -Werror -std=c11 -I. -I include \
 *       src/bignum_div_bignum.asm test_bignum_div_bignum.c \
 *       -o bin/test_bignum_div_bignum
 *
 * @history
 *   - rev. 0 (07.07.2026) : первая версия тестов.
 *   - rev. 1 (08.07.2026) : Исправлены ошибки с порядком аргументов и добавлены новые краевые случаи.
 *   - rev. 2 (08.07.2026) : Добавлен тест для ненормализованного делителя (ведущие нули).
 */

#include "bignum_div_bignum.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>   // PRIx64
#include <limits.h>     // UINT64_MAX

/* -------------------------------------------------------------------------- */
/* Тестовая инфраструктура                                                    */
/* -------------------------------------------------------------------------- */
static int tests_failed = 0;

#define RUN_TEST(test_func)                     \
    do {                                        \
        printf("--- Running test: %s ---\n", #test_func); \
        test_func();                            \
    } while (0)

#define ASSERT_TRUE(cond, msg)                  \
    do {                                        \
        if (!(cond)) {                          \
            printf("    [FAIL] %s\n", msg);     \
            ++tests_failed;                     \
        } else {                                \
            printf("    [PASS] %s\n", msg);     \
        }                                       \
    } while (0)

/* -------------------------------------------------------------------------- */
/* Вспомогательные функции                                                    */
/* -------------------------------------------------------------------------- */

/* Заполняет bignum_t значением 0…UINT64_MAX (одним словом). */
static void bignum_from_u64(bignum_t *bn, uint64_t v)
{
    memset(bn, 0, sizeof(*bn));
    if (v == 0) {
        bn->len = 0;
    } else {
        bn->len = 1;
        bn->words[0] = v;
    }
}

/* Сравнивает два bignum_t, учитывая len. */
static bool bignum_are_equal(const bignum_t *a, const bignum_t *b)
{
    if (a->len != b->len) return false;
    if (a->len == 0) return true;
    return memcmp(a->words, b->words, a->len * sizeof(uint64_t)) == 0;
}

/* -------------------------------------------------------------------------- */
/* Тесты «счастливого пути» и краевых случаев                                 */
/* -------------------------------------------------------------------------- */

static void test_happy_path_simple(void)
{
    /* 0x123456789ABCDEF1 / 0xFFFFFFFFFFFFFFFF */
    bignum_t numer, d, quot, rem, quot_exp, rem_exp;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 2;
    numer.words[0] = 0x0ULL;
    numer.words[1] = 0x123456789ABCDEF1ULL;

    bignum_from_u64(&d, UINT64_MAX);

    /* Expected quotient = N, remainder = N (because divisor = 2^64‑1) */
    bignum_from_u64(&quot_exp, 0);
    quot_exp.len = 1;
    quot_exp.words[0] = 0x123456789ABCDEF1ULL;

    bignum_from_u64(&rem_exp, 0x123456789ABCDEF1ULL);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &quot_exp), "Quotient correct");
    ASSERT_TRUE(bignum_are_equal(&rem, &rem_exp), "Remainder correct");
}

static void test_n_less_than_d(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&numer, 12345);
    bignum_from_u64(&d, 67890);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &(bignum_t){0}), "Quotient is zero");
    ASSERT_TRUE(bignum_are_equal(&rem, &(bignum_t){ .len = 1, .words = {12345} }), "Remainder equals N");
}

static void test_division_by_one(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 2;
    numer.words[0] = 0xAAAAAAAAAAAAAAAAULL;
    numer.words[1] = 0x1111111111111111ULL;

    bignum_from_u64(&d, 1);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &numer), "Quotient equals N");
    ASSERT_TRUE(bignum_are_equal(&rem, &(bignum_t){0}), "Remainder is zero");
}

static void test_max_values(void)
{
    /* N = 2^64, d = 2^64‑1 → Q = 1, R = 1 */
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 2;
    numer.words[0] = 0ULL;                // low word
    numer.words[1] = 1ULL;                // high word = 2^64

    bignum_from_u64(&d, UINT64_MAX);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &(bignum_t){ .len = 1, .words = {1} }), "Quotient correct for max values");
    ASSERT_TRUE(bignum_are_equal(&rem, &(bignum_t){ .len = 1, .words = {1} }), "Remainder correct for max values");
}

static void test_leading_zeros_in_dividend(void)
{
    /* N = 10·2^64 + 5, d = 10 → Q = 2^64, R = 5 */
    bignum_t numer, d, quot, rem, quot_exp, rem_exp;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 3;
    numer.words[2] = 0ULL;               // leading zero (must be ignored)
    numer.words[1] = 10ULL;
    numer.words[0] = 5ULL;

    bignum_from_u64(&d, 10);

    bignum_from_u64(&quot_exp, 0);
    quot_exp.len = 2;
    quot_exp.words[1] = 1ULL;            // 2^64
    quot_exp.words[0] = 0ULL;

    bignum_from_u64(&rem_exp, 5);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &quot_exp), "Quotient correct with leading zeros");
    ASSERT_TRUE(bignum_are_equal(&rem, &rem_exp), "Remainder correct with leading zeros");
}

static void test_leading_zeros_in_divisor(void)
{
    /* N = 100, d = 10 (с ведущим нулем) → Q = 10, R = 0 */
    bignum_t numer, d, quot, rem, quot_exp, rem_exp;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&numer, 100);

    memset(&d, 0, sizeof(d));
    d.len = 2;
    d.words[1] = 0ULL;                   // leading zero (must be ignored)
    d.words[0] = 10ULL;

    bignum_from_u64(&quot_exp, 10);
    bignum_from_u64(&rem_exp, 0);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &quot_exp), "Quotient correct with leading zeros in divisor");
    ASSERT_TRUE(bignum_are_equal(&rem, &rem_exp), "Remainder correct with leading zeros in divisor");
}

static void test_zero_dividend(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&numer, 0);
    bignum_from_u64(&d, 12345);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &(bignum_t){0}), "Quotient is zero");
    ASSERT_TRUE(bignum_are_equal(&rem, &(bignum_t){0}), "Remainder is zero");
}

static void test_large_n_less_than_d(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 2;
    numer.words[0] = 0xFFFFFFFFFFFFFFFFULL;
    numer.words[1] = 0x0000000000000001ULL;

    memset(&d, 0, sizeof(d));
    d.len = 3;
    d.words[0] = 0ULL;
    d.words[1] = 0ULL;
    d.words[2] = 1ULL;

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &(bignum_t){0}), "Quotient is zero");
    ASSERT_TRUE(bignum_are_equal(&rem, &numer), "Remainder equals N");
}

static void test_multi_word_division(void)
{
    /* N = 2^128 + 2^64 + 1, D = 2^64 */
    /* Q = 2^64 + 1, R = 1 */
    bignum_t numer, d, quot, rem, quot_exp, rem_exp;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    memset(&numer, 0, sizeof(numer));
    numer.len = 3;
    numer.words[0] = 1ULL;
    numer.words[1] = 1ULL;
    numer.words[2] = 1ULL;

    memset(&d, 0, sizeof(d));
    d.len = 2;
    d.words[0] = 0ULL;
    d.words[1] = 1ULL;

    memset(&quot_exp, 0, sizeof(quot_exp));
    quot_exp.len = 2;
    quot_exp.words[0] = 1ULL;
    quot_exp.words[1] = 1ULL;

    bignum_from_u64(&rem_exp, 1ULL);

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &quot_exp), "Multi-word quotient correct");
    ASSERT_TRUE(bignum_are_equal(&rem, &rem_exp), "Multi-word remainder correct");
}

/* -------------------------------------------------------------------------- */
/* Тесты обработки ошибок                                                     */
/* -------------------------------------------------------------------------- */

static void test_error_null_pointer(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));
    bignum_from_u64(&numer, 10);
    bignum_from_u64(&d, 1);

    ASSERT_TRUE(bignum_div_bignum(NULL, &d, &quot, &rem) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR, "Handles NULL dividend");
    ASSERT_TRUE(bignum_div_bignum(&numer, NULL, &quot, &rem) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR, "Handles NULL divisor");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, NULL, &rem) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR, "Handles NULL quotient");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, NULL) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR, "Handles NULL remainder");
}

static void test_error_division_by_zero(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&numer, 10);
    bignum_from_u64(&d, 0); // Делитель равен 0

    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &rem) == BIGNUM_DIV_BIGNUM_ERR_DIVISION_BY_ZERO, "Handles division by zero");
}

static void test_error_buffer_overlap(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&numer, 10);
    bignum_from_u64(&d, 2);

    /* Проверяем все возможные пересечения буферов */
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &numer, &rem) == BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP, "Overlap: quotient == dividend");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &d, &rem) == BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP, "Overlap: quotient == divisor");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &numer) == BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP, "Overlap: remainder == dividend");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &d) == BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP, "Overlap: remainder == divisor");
    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &quot) == BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP, "Overlap: quotient == remainder");
}

static void test_error_bad_length(void)
{
    bignum_t numer, d, quot, rem;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    bignum_from_u64(&d, 123);

    memset(&numer, 0, sizeof(numer));
    numer.len = BIGNUM_CAPACITY + 1; /* Заведомо неверная длина */

    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &rem) == BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH, "Handles bad length in dividend");

    bignum_from_u64(&numer, 123);
    memset(&d, 0, sizeof(d));
    d.len = BIGNUM_CAPACITY + 1; /* Заведомо неверная длина в делителе */

    ASSERT_TRUE(bignum_div_bignum(&numer, &d, &quot, &rem) == BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH, "Handles bad length in divisor");
}

static void test_2048th_bit_overflow(void)
{
    bignum_t numer, d, quot, rem, quot_exp, rem_exp;
    memset(&quot, 0, sizeof(quot));
    memset(&rem, 0, sizeof(rem));

    /* Делитель D = 2^2047 + 1 
       Устанавливаем самый старший бит (2047-й), чтобы спровоцировать переполнение R */
    memset(&d, 0, sizeof(d));
    d.len = 32;
    d.words[31] = 0x8000000000000000ULL;
    d.words[0] = 1ULL;

    /* Делимое N = 2^2048 - 1 (максимально возможное 2048-битное число, все биты = 1) */
    memset(&numer, 0xFF, sizeof(numer));
    numer.len = 32;

    /* Ожидаемое частное Q = 1, так как N >= D, но N < 2*D */
    memset(&quot_exp, 0, sizeof(quot_exp));
    quot_exp.len = 1;
    quot_exp.words[0] = 1ULL;

    /* Ожидаемый остаток R = N - D = (2^2048 - 1) - (2^2047 + 1) = 2^2047 - 2 */
    memset(&rem_exp, 0xFF, sizeof(rem_exp));
    rem_exp.len = 32;
    rem_exp.words[31] = 0x7FFFFFFFFFFFFFFFULL;
    rem_exp.words[0] = 0xFFFFFFFFFFFFFFFEULL;

    bignum_div_bignum_status_t st = bignum_div_bignum(&numer, &d, &quot, &rem);
    ASSERT_TRUE(st == BIGNUM_DIV_BIGNUM_OK, "Status OK");
    ASSERT_TRUE(bignum_are_equal(&quot, &quot_exp), "Quotient correct for 2048-th bit overflow");
    ASSERT_TRUE(bignum_are_equal(&rem, &rem_exp), "Remainder correct for 2048-th bit overflow");
}


/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Running Deterministic Tests for bignum_div_bignum ===\n");

    RUN_TEST(test_happy_path_simple);
    RUN_TEST(test_n_less_than_d);
    RUN_TEST(test_division_by_one);
    RUN_TEST(test_max_values);
    RUN_TEST(test_leading_zeros_in_dividend);
    RUN_TEST(test_leading_zeros_in_divisor);
    RUN_TEST(test_zero_dividend);
    RUN_TEST(test_large_n_less_than_d);
    RUN_TEST(test_multi_word_division);
    RUN_TEST(test_2048th_bit_overflow);

    RUN_TEST(test_error_null_pointer);
    RUN_TEST(test_error_division_by_zero);
    RUN_TEST(test_error_buffer_overlap);
    RUN_TEST(test_error_bad_length);

    printf("----------------------------------------\n");
    if (tests_failed == 0) {
        printf("All tests passed!\n");
    } else {
        printf("%d test(s) failed.\n", tests_failed);
    }
    printf("========================================\n");
    return tests_failed == 0 ? 0 : 1;
}
