# Companion guide: `profiles/standard.json`

Файл [`standard.json`](standard.json) — короткий, но репрезентативный manifest schema version `1` для smoke-проверки framework. Он не является универсальной моделью производительности; его назначение — быстро проверить, что adapter корректно принимает все workload dimensions, C11 matrix-runner сохраняет JSON и ST/MT protocol выполняется в каждом сценарии.

> Каждый profile содержит только declarative metadata. Семантику `noop`, `xor`, `rotate`, `tiny`, `medium` и `near-capacity` определяет adapter потребляющей библиотеки.

| Profile ID | Назначение | Ключевая ось |
|---|---|---|
| `zero-tiny-noop-end-to-end` | Проверяет минимальный input и полную lifecycle границу | zero input + end-to-end |
| `nonzero-small-xor-kernel` | Проверяет короткую обычную операцию | small state + kernel-only |
| `nonzero-medium-rotate-kernel` | Проверяет средний mutable state | medium state + kernel-only |
| `nonzero-large-xor-end-to-end` | Проверяет preparation/copy при крупном state | large state + end-to-end |
| `nonzero-near-capacity-rotate-kernel` | Проверяет безопасный capacity boundary | near-capacity + kernel-only |
| `mixed-variable-mixed-end-to-end` | Проверяет seed-dependent variable state | mixed input + variable size |

## How-to 1. Запустить стандартный smoke через Makefile

Соберите framework и выполните одну повторность с короткой нагрузкой. Значение `BENCH_MATRIX_MT_TOTAL_ITERATIONS` должно делиться на `MT_THREADS`.

```bash
make bench_matrix CONFIG=release \
  REPORT_NAME=standard_smoke \
  BENCH_MATRIX_PROFILE=profiles/standard.json \
  BENCH_MATRIX_REPETITIONS=1 \
  BENCH_MATRIX_ITERATIONS=1001 \
  BENCH_MATRIX_MT_TOTAL_ITERATIONS=2000 \
  BENCH_MATRIX_WARMUP=10 \
  BENCH_MATRIX_DATA_COUNT=32 \
  MT_THREADS=2 \
  BENCH_MATRIX_TIMEOUT_SECONDS=20
```

Команда создаёт `benchmarks/reports/standard_smoke_matrix.json` и `benchmarks/reports/standard_smoke_matrix_summary.json`. Первый artifact содержит raw samples, второй — metrics по каждой паре `profile_id × mode`.

## How-to 2. Подключить тот же manifest к bignum adapter

Сначала убедитесь, что bignum adapter интерпретирует все значения standard manifest. Например, для `bignum_shift_right` можно определить `noop` как shift на 0 bits, `xor` как bit shift на 13 bits, `rotate` как combined word+bit shift, `large` как `BIGNUM_CAPACITY - 2` words и `near-capacity` как `BIGNUM_CAPACITY - 1` words. Подробный adapter показан в [../docs/HOW_TO_BIGNUM.md](../docs/HOW_TO_BIGNUM.md).

Затем запускайте тот же C11 executable с bignum ST/MT binaries.

```bash
build/tools/bench_matrix \
  --manifest profiles/standard.json \
  --output benchmarks/reports/bignum_standard_matrix.json \
  --st-binary build/bench_bignum_shift_right_st \
  --mt-binary build/bench_bignum_shift_right_mt \
  --repetitions 3 --iterations 5000000 --mt-total-iterations 10000000 \
  --threads 2 --warmup 10000 --data-count 4096 \
  --seed 0x9E3779B97F4A7C15 --timeout-seconds 1800
```

## How-to 3. Сделать reviewed baseline

Используйте фиксированные compiler flags, CPU affinity, profile file, seed, thread count и iteration counts. Сначала сохраните raw matrix, затем агрегируйте его отдельной C11 командой. Не перезаписывайте baseline автоматически новым candidate run.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_standard_matrix.json \
  --output benchmarks/reports/bignum_standard_reviewed_summary.json
```

Следующий candidate сравнивается с сохранённым summary или с raw matrix. Regression требует одновременно превышения percentage threshold и MAD noise floor baseline.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_candidate_matrix.json \
  --baseline benchmarks/reports/bignum_standard_reviewed_summary.json \
  --output benchmarks/reports/bignum_candidate_summary.json \
  --threshold-pct 5 --noise-mad-multiplier 3
```

## Как изменить manifest безопасно

Добавляйте profile только с уникальным `id`, строковыми non-empty workload fields и schema version `1`. Любое изменение profile set изменяет baseline contract: новый candidate должен быть сравнён с baseline, содержащим тот же набор `profile_id × st|mt`. Для расширенных length/near-capacity сценариев используйте [`full.json`](full.json) и его отдельную документацию [full.json.md](full.json.md).
