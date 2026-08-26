/**
 * @file bench_bignum_div_bignum_mt.c
 * @brief Multi-thread benchmark-framework entrypoint for bignum subtraction.
 */
#include <stdlib.h>
#include <benchmark_framework.h>
#include "adapter/bignum_div_bignum_benchmark_adapter.h"

int main(int argc, char **argv)
{
    benchmark_adapter_t adapter;
    bignum_div_bignum_benchmark_status_t adapter_status;
    benchmark_core_status_t core_status;

    adapter_status = bignum_div_bignum_benchmark_adapter_init(&adapter);
    if (adapter_status != BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS) return EXIT_FAILURE;
    core_status = benchmark_core_run_mt(argc, argv, &adapter);
    return core_status == BENCHMARK_CORE_STATUS_SUCCESS ||
           core_status == BENCHMARK_CORE_STATUS_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
}
