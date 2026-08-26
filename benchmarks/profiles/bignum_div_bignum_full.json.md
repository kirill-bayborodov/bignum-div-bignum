# bignum_div_bignum full benchmark profile

This companion document describes `profiles/bignum_div_bignum_full.json`. The manifest is consumed by `benchmark-framework` and contains deterministic workload dimensions, operation metadata, and execution modes.

## Validation

The adapter rejects null required fields and invalid operation identifiers before worker execution. The benchmark publishes a checksum only after the operation completes.

## Reproducible command

```sh
make bench_matrix CONFIG=release USE_ASM=no BENCH_MATRIX_PROFILE=benchmarks/profiles/bignum_div_bignum_full.json BENCH_MATRIX_REPETITIONS=1 BENCH_MATRIX_ITERATIONS=20 BENCH_MATRIX_MT_TOTAL_ITERATIONS=40 BENCH_MATRIX_WARMUP=1 BENCH_MATRIX_DATA_COUNT=1 BENCH_MATRIX_TIMEOUT_SECONDS=30
```

Repeat with `USE_ASM=yes` and compare median or mean `ns_per_call` values for identical profile IDs and modes. A run is accepted only when all samples report successful operations and matching fingerprints between C11 and ASM.
