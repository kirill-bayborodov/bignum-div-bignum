/**
 * @file    test_bignum_div_bignum_extra.c
 * @author  git@bayborodov.com
 * @version 1.0.0
 * @date    08.07.2026
 *
 * @brief   Расширенные тесты для модуля bignum_div_bignum
 *
 * @details
 *   Проверяем:
 *    - NULL-параметры
 *    - Контракт: len > BIGNUM_CAPACITY
 *    - Отсутствие записи за пределы буферов (memory guard check)
 */

#include "bignum_div_bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Вспомогательная функция для сравнения
/*static int bignum_are_equal(const bignum_t* a, const bignum_t* b) {
    if (a == NULL || b == NULL) {
        return a == b;
    }
    if (a->len != b->len) {
        return 0;
    }
    if (a->len == 0) {
        return 1;  // оба пустые
    }
    return memcmp(a->words, b->words, a->len * sizeof(uint64_t)) == 0;
}*/

// 1. Тест: NULL-параметры
static void test_null_arg(void) {
    printf("test_null_arg...");
    bignum_t n = {0}, d = {0}, q = {0}, r = {0};
    d.len = 1; 
    d.words[0] = 1;
    
    assert(bignum_div_bignum(NULL, &d, &q, &r) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR);
    assert(bignum_div_bignum(&n, NULL, &q, &r) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR);
    assert(bignum_div_bignum(&n, &d, NULL, &r) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR);
    assert(bignum_div_bignum(&n, &d, &q, NULL) == BIGNUM_DIV_BIGNUM_ERR_NULL_PTR);
    printf("OK\n");
}

// 2. Тест: len > BIGNUM_CAPACITY (нарушение контракта)
static void test_contract_violation_len_overflow(void) {
    printf("test_contract_violation_len_overflow...");
    bignum_t n = {0}, d = {0}, q = {0}, r = {0};
    
    n.len = BIGNUM_CAPACITY + 1;
    d.len = 1; 
    d.words[0] = 1;
    
    // Ассемблерная реализация явно проверяет длину и возвращает ошибку
    int rc = bignum_div_bignum(&n, &d, &q, &r);
    assert(rc == BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH);
    
    n.len = 1;
    d.len = BIGNUM_CAPACITY + 1;
    rc = bignum_div_bignum(&n, &d, &q, &r);
    assert(rc == BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH);
    
    printf("OK\n");
}

// 3. Тест: Проверка, что функция не пишет за границами своих буферов (quotient и remainder)
static void test_memory_guard_check(void) {
    printf("test_memory_guard_check...");

    uint64_t guard_val = 0xDEADBEEFDEADBEEF;
    size_t buffer_size = sizeof(bignum_t) + 2 * sizeof(uint64_t);
    
    char* q_buf = (char*)malloc(buffer_size);
    char* r_buf = (char*)malloc(buffer_size);

    uint64_t* q_guard1 = (uint64_t*)q_buf;
    bignum_t* q = (bignum_t*)(q_buf + sizeof(uint64_t));
    uint64_t* q_guard2 = (uint64_t*)((char*)q + sizeof(bignum_t));

    uint64_t* r_guard1 = (uint64_t*)r_buf;
    bignum_t* r = (bignum_t*)(r_buf + sizeof(uint64_t));
    uint64_t* r_guard2 = (uint64_t*)((char*)r + sizeof(bignum_t));

    *q_guard1 = guard_val;
    *q_guard2 = guard_val;
    *r_guard1 = guard_val;
    *r_guard2 = guard_val;

    bignum_t n = {0}, d = {0};
    n.len = 1; n.words[0] = 100;
    d.len = 1; d.words[0] = 3;

    int rc = bignum_div_bignum(&n, &d, q, r);
    assert(rc == BIGNUM_DIV_BIGNUM_OK);
    
    assert(q->len == 1 && q->words[0] == 33);
    assert(r->len == 1 && r->words[0] == 1);

    // Проверяем, что "сторожевые" значения не затерты
    assert(*q_guard1 == guard_val);
    assert(*q_guard2 == guard_val);
    assert(*r_guard1 == guard_val);
    assert(*r_guard2 == guard_val);

    free(q_buf);
    free(r_buf);
    printf("OK\n");
}

int main(void) {
    printf("=== Extra tests for bignum_div_bignum ===\n");
    test_null_arg();
    test_contract_violation_len_overflow();
    test_memory_guard_check();
    printf("=== All extra tests passed ===\n");
    return EXIT_SUCCESS;
}
