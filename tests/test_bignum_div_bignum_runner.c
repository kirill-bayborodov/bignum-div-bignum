/**
 * @file    test_bignum_div_bignum_runner.c
 * @author  git@bayborodov.com
 * @version 1.0.0
 * @date    08.07.2026
 *
 * @brief Интеграционный тест‑раннер для библиотеки.
 * @details Применяется для проверки достаточности сигнатур
 *          в файле заголовка (header) при сборке и линковке.
 */

#include "bignum_div_bignum.h"
#include <assert.h>
#include <stdio.h>

int main() {
    printf("Running test: test_bignum_div_bignum_runner... ");
    
    bignum_t n = {0}, d = {0}, q = {0}, r = {0};
    d.len = 1;
    d.words[0] = 1;
    
    bignum_div_bignum(&n, &d, &q, &r);
    
    assert(1);
    printf("PASSED\n");
    return 0;
}
