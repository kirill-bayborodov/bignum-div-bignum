/**
 * @file bench_bignum_div_bignum.c
 * @brief Single-thread benchmark-framework entrypoint for bignum subtraction.
 */
#include <benchmark_framework.h>
#include "bignum_div_bignum_benchmark_adapter.h"

int main(int argc, char **argv)
{
    benchmark_adapter_t adapter;
    if (bignum_div_bignum_benchmark_adapter_init(&adapter) != BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS) {
        return 2;
    }
    benchmark_core_status_t status = benchmark_core_run_st(argc, argv, &adapter);
    return status == BENCHMARK_CORE_STATUS_SUCCESS || status == BENCHMARK_CORE_STATUS_HELP
        ? 0 : 1;
}
