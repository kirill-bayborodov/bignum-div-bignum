/**
 * @file test_bignum_div_bignum_benchmark_adapter.c
 * @brief Deterministic tests for the bignum_div_bignum benchmark adapter.
 * @details The test uses the adapter's opaque state_size and never depends on
 * private state layout. It checks workload validation, callback construction,
 * deterministic initialization, operation success and observable checksum.
 * @version 1.0.0
 */
#include "bignum_div_bignum_benchmark_adapter.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Builds a complete valid workload fixture. */
static benchmark_workload_t make_workload(void)
{
    return (benchmark_workload_t){
        .data_mode = "custom",
        .input_kind = "nonzero",
        .operation_kind = "default",
        .measure_mode = "kernel-only",
        .size_profile = "quarter",
        .capacity_profile = "normal",
        .seed = UINT64_C(11400714819323198485),
        .warmup = 5U,
        .data_count = 16U
    };
}

/** @brief Checks valid, invalid and NULL workload validation statuses. */
static void test_validation(void)
{
    benchmark_workload_t workload = make_workload();
    benchmark_workload_t invalid = workload;
    assert(bignum_div_bignum_benchmark_validate_workload(&workload) ==
        BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS);
    invalid.operation_kind = "xor";
    assert(bignum_div_bignum_benchmark_validate_workload(&invalid) ==
        BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_INVALID_PROFILE);
    assert(bignum_div_bignum_benchmark_validate_workload(NULL) ==
        BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_NULL_ARGUMENT);
}

/**
 * @brief Checks benchmark-core legacy mode aliases accepted by the adapter.
 * @details benchmark-core maps all_zero, all_nonzero and mixed to noop/tiny,
 * default/medium and mixed/variable workload tokens before callbacks run.
 */
static void test_legacy_modes(void)
{
    static const char *const operations[] = { "noop", "default", "mixed" };
    static const char *const sizes[] = { "tiny", "medium", "variable" };
    benchmark_workload_t workload = make_workload();

    for (size_t index = 0U; index < 3U; ++index) {
        workload.operation_kind = operations[index];
        workload.size_profile = sizes[index];
        assert(bignum_div_bignum_benchmark_validate_workload(&workload) ==
            BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS);
    }
}

/** @brief Checks deterministic callback initialization and operation lifecycle. */
static void test_callbacks(void)
{
    benchmark_adapter_t adapter;
    benchmark_workload_t workload = make_workload();
    unsigned char *first;
    unsigned char *second;
    uint64_t first_checksum;
    uint64_t second_checksum;
    assert(bignum_div_bignum_benchmark_adapter_init(NULL) ==
        BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_NULL_ARGUMENT);
    assert(bignum_div_bignum_benchmark_adapter_init(&adapter) ==
        BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS);
    assert(adapter.initialize != NULL && adapter.operation != NULL && adapter.checksum != NULL);
    assert(adapter.state_size > sizeof(uint64_t));
    first = calloc(1U, adapter.state_size);
    second = calloc(1U, adapter.state_size);
    assert(first != NULL && second != NULL);
    assert(adapter.initialize(first, 3U, &workload, adapter.adapter_context) ==
        BENCHMARK_ADAPTER_STATUS_SUCCESS);
    assert(adapter.initialize(second, 3U, &workload, adapter.adapter_context) ==
        BENCHMARK_ADAPTER_STATUS_SUCCESS);
    assert(memcmp(first, second, adapter.state_size) == 0);
    assert(adapter.operation(first, 7U, &workload, adapter.adapter_context) ==
        BENCHMARK_ADAPTER_STATUS_SUCCESS);
    first_checksum = adapter.checksum(first, 7U, adapter.adapter_context);
    second_checksum = adapter.checksum(second, 7U, adapter.adapter_context);
    assert(first_checksum != 0U);
    assert(first_checksum != second_checksum);
    free(first);
    free(second);
}

/** @brief Runs all deterministic adapter assertions. */
int main(void)
{
    test_validation();
    test_legacy_modes();
    test_callbacks();
    puts("bignum_div_bignum benchmark adapter tests: OK");
    return EXIT_SUCCESS;
}
