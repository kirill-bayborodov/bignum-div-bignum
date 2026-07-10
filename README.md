# bignum-div-bignum

[![C/ASM CI](https://github.com/kirill-bayborodov/bignum-div-bignum/actions/workflows/ci.yml/badge.svg)](https://github.com/kirill-bayborodov/bignum-div-bignum/actions/workflows/ci.yml)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/kirill-bayborodov/bignum-div-bignum?label=release)](https://github.com/kirill-bayborodov/bignum-div-bignum/releases/latest)

`bignum-div-bignum` is a high-performance, standalone module for performing division of an arbitrary-precision integer (`bignum_t`) by another arbitrary-precision integer.
A highly optimized x86-64 assembly implementation of a bignum division operation, designed for performance-critical applications.

## Distribution

Part of the `bignum-lib` project: https://github.com/kirill-bayborodov/bignum-lib
Also available as a standalone distribution.

## Features

-   **High Performance:** Hand-crafted x86-64 yasm assembly — an ultra-optimized, multithreading-ready engine delivering peak execution speed.
-   **Dependency-Free Core:** The core logic has no external runtime dependencies.
-   **Tests and Benchmarks:** Provides a comprehensive test suite and performance microbenchmarks.
-   **Automated Builds:** A comprehensive `Makefile` for easy compilation, testing, and benchmarking.
-   **Continuous Integration:** All changes are automatically built and tested via GitHub Actions.
-   **Static Analysis:** Code quality is enforced using `cppcheck` for all C-based test files.

## Architecture & Optimizations

This module is heavily optimized for modern x86-64 processors. Key architectural decisions include:

- **Knuth's Algorithm D:** The core division logic implements Donald Knuth's Algorithm D (from *The Art of Computer Programming, Vol. 2*). It normalizes the operands, estimates the quotient word-by-word, and performs highly efficient multiply-subtract loops.
- **Loop Unrolling:** The hottest execution path (the multiply-subtract inner loop) is unrolled (x2) to minimize loop control overhead (branching and increments) and maximize Instruction-Level Parallelism (ILP).
- **Avoiding Microcoded Shifts:** Bitwise shifts across word boundaries (during normalization and denormalization) deliberately avoid the `shld`/`shrd` instructions. Although these instructions reduce code size, they are microcoded on modern architectures (Intel Core, AMD Zen) and incur higher latency. Using explicit `shl`, `shr`, and `or` sequences yields significantly better throughput.
- **Optimized Memory Access (RMW):** Direct Read-Modify-Write memory operations (e.g., `sub [mem], reg`) in hot loops are replaced with explicit `mov -> sub -> mov` sequences. This reduces pressure on the CPU's Store Buffers and Load/Store units, giving the Out-of-Order (OoO) scheduler more freedom and preventing resource stalls, especially in multithreaded (Hyper-Threading) environments.

## Dependencies

-   **Build-time:** `make`, `gcc`, `yasm`, `cppcheck`.
-   **Component:** This project requires `bignum-common` as a git submodule located at `libs/bignum-common`.

To clone the repository with its submodule, use:
```bash
git clone --recurse-submodules https://github.com/kirill-bayborodov/bignum-div-bignum.git
```

## API

The library provides a single function, declared in `include/bignum_div_bignum.h`.

```c
bignum_div_bignum_status_t bignum_div_bignum(const bignum_t *dividend,
                                             const bignum_t *divisor,
                                             bignum_t *quotient,
                                             bignum_t *remainder);
```
-   **`dividend`**: A pointer to the `bignum_t` structure representing the dividend.
-   **`divisor`**: A pointer to the `bignum_t` structure representing the divisor.
-   **`quotient`**: A pointer to the `bignum_t` structure where the quotient will be stored.
-   **`remainder`**: A pointer to the `bignum_t` structure where the remainder will be stored.
-   **Returns**: A `bignum_div_bignum_status_t` enum (`BIGNUM_DIV_BIGNUM_OK`, `BIGNUM_DIV_BIGNUM_ERR_NULL_PTR`, `BIGNUM_DIV_BIGNUM_ERR_DIVISION_BY_ZERO`, `BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP`, `BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH`, `BIGNUM_DIV_BIGNUM_ERR_OVERFLOW`).

## How to Build, Test, Install and Use

The project uses a `Makefile` to manage all tasks.

### Build the code
Builds the assembly object file.
```bash
make build CONFIG=release
```

### Run Unit Tests
Compiles and runs fast, essential correctness tests.
```bash
make test CONFIG=release
```

### Run Static Analysis
Checks all C source files (`tests/`, `benchmarks/` and `dist/`) for potential bugs and style issues.
```bash
make lint
```

### Run Performance Benchmarks
Compiles and runs performance tests using `perf`. The txt report is saved to `benchmarks/reports/check_*.txt`.
```bash
make bench CONFIG=debug
```

### Build the distributive
Builds the installation pack of files (with objects .o file) in dist directory.
```bash
make install CONFIG=release
```

### Build the distributive
Builds the distributive pack of files (with single-header and static library .a file).
```bash
make dist CONFIG=release
```

## Clean Up

To remove all generated files (object files, executables, reports):
```bash
make clean
```

## How to Use

This project produces an object file (`bignum_div_bignum.o`) which you can link with your own application.

**1. Clone the repository with submodules:**
```bash
git clone --recurse-submodules https://github.com/kirill-bayborodov/bignum-div-bignum.git
cd bignum-div-bignum
```

**2. Build the object file:**
```bash
make build
```
The output will be located at `build/bignum_div_bignum.o`.

**3. Link with your application:**
When compiling your project, include the object file and specify the include paths for the headers.
```bash
gcc your_app.c build/bignum_div_bignum.o -I./include -I./libs/bignum-common/include -o your_app -no-pie
```

## Contributing

Contributions are welcome! Please follow these steps:
1.  Fork the repository.
2.  Create a new branch (`git checkout -b feature/AmazingFeature`).
3.  Make your changes.
4.  Commit your changes (`git commit -m 'Add some AmazingFeature'`).
5.  Push to the branch (`git push origin feature/AmazingFeature`).
6.  Open a Pull Request.

When creating Issues or Pull Requests, please use the provided templates to ensure all necessary information is included.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
```

