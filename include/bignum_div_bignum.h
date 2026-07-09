/**
 * @file    bignum_div_bignum.h
 * @author  git@bayborodov.com
 * @version 1.0.0
 * @date    10.04.2026
 *
 * @brief   Публичный API для деления больших целых bignum_t.
 *
 * @details
 *   Определяет API для функции bignum_div_bignum, включая типы данных,
 *   коды состояния и прототипы функций. Нормализация (удаление ведущих нулей)
 *   выполняется автоматически.
 *
 *   Функция является потокобезопасной при условии, что разные потоки
 *   работают с разными, не пересекающимися объектами `bignum_t`.
 *
 *
 * @see     bignum.h
 * @since   1.0.0
 *
 * @history
 *   - rev. 1 (07.04.2026): Первоначальное создание API.
 *   - rev. 2 (10.04.2026): Уточнение кодов состояния и сигнатуры с учетом bignum_t.
 */

#ifndef BIGNUM_DIV_BIGNUM_H
#define BIGNUM_DIV_BIGNUM_H

#include <bignum.h>
#include <stddef.h>
#include <stdint.h>

// Проверка на наличие определения BIGNUM_CAPACITY из общего заголовка
#ifndef BIGNUM_CAPACITY
#  error "bignum.h must define BIGNUM_CAPACITY"
#endif

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Коды состояния для функции bignum_div_bignum.
 */
typedef enum {
    BIGNUM_DIV_BIGNUM_OK                    =  0,
    BIGNUM_DIV_BIGNUM_ERR_NULL_PTR          = -1,
    BIGNUM_DIV_BIGNUM_ERR_DIVISION_BY_ZERO  = -2,
    BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP    = -3,
    BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH        = -4,/** @brief Ошибка: длина входного числа n->len превышает BIGNUM_CAPACITY. */
    BIGNUM_DIV_BIGNUM_ERR_OVERFLOW          = -5 /**< Результат не помещается в BIGNUM_CAPACITY */
} bignum_div_bignum_status_t;


/**
 * @brief Деление больших беззнаковых целых: quotient = dividend / divisor, remainder = dividend % divisor.
 *
 * @details
 *   Поддерживает нормализацию входных данных, проверку ошибок и потокобезопасность.
 *   Частное и остаток автоматически нормализуются.
 *
 * @param[in]  dividend Указатель на делимое (bignum_t).
 * @param[in]  divisor  Указатель на делитель (bignum_t).
 * @param[out] quotient Указатель на структуру для записи частного (bignum_t).
 * @param[out] remainder Указатель на структуру для записи остатка (bignum_t).
 *
 * @return Код состояния bignum_div_bignum_status_t.
 * ABI: соответствует SysV x86-64; см. реализацию YASM.
 */
bignum_div_bignum_status_t bignum_div_bignum(const bignum_t *dividend,
                                             const bignum_t *divisor,
                                             bignum_t *quotient,
                                             bignum_t *remainder);

#ifdef __cplusplus
}
#endif

#endif /* BIGNUM_DIV_BIGNUM_H */
