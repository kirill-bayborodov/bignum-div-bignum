# bignum-div-bignum

## Overview

Divides one fixed-capacity unsigned big integer by another. The module uses the little-endian `bignum_t` representation supplied by `bignum-core`. It exposes a deterministic C11 reference implementation and the repository's x86-64 System V YASM implementation where available.

## Features

The API has one operation-specific public entry point, explicit named status codes, fixed-capacity storage, caller-owned inputs and caller-allocated outputs. Successful calls publish a complete result. Failure paths are transactional unless the public header explicitly defines an in-place operation.

## Representation and Contract

`bignum_t` stores `BIGNUM_CAPACITY` little-endian 64-bit words and a logical `len`. Inputs are borrowed and remain owned by the caller. The library does not allocate or free caller storage. Pointers must remain valid for the duration of the call. Each status code in the public header defines whether outputs remain unchanged, are zeroed, or contain a published result.

## API

The primary operation is:

```c
#include "bignum_div_bignum.h"
bignum_div_bignum(/* see the public header for the exact typed parameters */);
```

The public header is authoritative for parameter direction, aliasing, overflow, normalization, and status semantics. No undocumented global state is used; independent calls are thread-safe when their objects do not overlap.

## Dependencies

The module depends on the following local components:

| Component | Role |
|---|---|
| `bignum-core` | `bignum_t` representation and capacity definition |
| `bignum-init` / `bignum-init-u64` | Canonical test and example initialization |
| `bignum-init-from-array` | Deterministic vector construction |
| `bignum-normalize` | Canonical logical length handling |
| `bignum-cmp` | Comparison oracle where required |
| `benchmark-framework` | Standard and full benchmark matrix execution |

The former `bignum-common` dependency is intentionally not used.

## Build

The Makefile is adopted from the `bignum-bit-test` template and is frozen after adoption. Build the release targets with:

```sh
make build CONFIG=release USE_ASM=no
make test CONFIG=release USE_ASM=no
```

Use `USE_ASM=yes` to build the assembly implementation. The Makefile discovers local submodules and links their distributions without repository-specific edits.

## Tests and Coverage

The test suite includes deterministic vectors, boundary and overflow cases, aliasing/guard checks where applicable, randomized robustness checks, multithreaded execution, runner integration, and benchmark-adapter validation. C11 coverage is measured in a separate instrumented build so the frozen Makefile remains unchanged:

```sh
make test CONFIG=release USE_ASM=no CC='gcc --coverage' LDFLAGS='--coverage -no-pie -lm'
gcov -b -c build/bignum_div_bignum.gcda
```

Coverage acceptance is greater than 90% for the operation source; uncovered defensive branches must be explained in the review record.

## Benchmarks

The benchmark adapter supplies deterministic operands, validates profile fields, executes the operation callback, and publishes a checksum after completion. The standard matrix can be run as follows:

```sh
make bench_matrix CONFIG=release USE_ASM=no \
  BENCH_MATRIX_PROFILE=benchmarks/profiles/bignum_div_bignum_standard.json \
  BENCH_MATRIX_REPETITIONS=1 BENCH_MATRIX_ITERATIONS=20 \
  BENCH_MATRIX_MT_TOTAL_ITERATIONS=40 BENCH_MATRIX_WARMUP=1 \
  BENCH_MATRIX_DATA_COUNT=1 BENCH_MATRIX_TIMEOUT_SECONDS=30
```

Run the same command with `USE_ASM=yes` for the assembly comparison. Reports are written under `benchmarks/reports/`; compare like-for-like profile and execution-mode keys only.

## C11 and Assembly Boundary

The C11 implementation is the correctness reference. The YASM entry point follows the System V AMD64 ABI: integer and pointer arguments use the standard registers, callee-saved registers are preserved, the stack remains aligned at call boundaries, and the named status value is returned in `RAX`. The assembly implementation must preserve the same `bignum_t` memory layout and observable error behavior.

## Error Handling and Security

Inputs are validated before publication of results. Capacity overflow, invalid lengths, null pointers, division by zero, and forbidden overlap are reported through named statuses defined by the header. No partial result is exposed on an error path. The implementation uses bounded fixed-size storage and does not perform unchecked dynamic allocation.

## Documentation Quality Gates

Public headers, C and ASM boundary comments, tests, benchmark adapters, JSON manifests, and this README are reviewed against `QUALITY_GATES_DOCUMENTATION_C11_JSON.md`. Commands in this document are intended to be copyable without manual correction.

## License

See the repository license file.
