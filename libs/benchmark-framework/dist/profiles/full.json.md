# Companion guide: `profiles/full.json`

Файл [`full.json`](full.json) — расширенный manifest schema version `1` для проверяемого performance/regression прогона. В отличие от [`standard.json`](standard.json), он одновременно покрывает zero/nonzero/mixed input, tiny/small/medium/large/variable size и multiple near-capacity paths. При одном repetition matrix produces 24 samples: двенадцать profiles, каждый в ST и MT mode.

| Группа profiles | Что изолирует | Почему это важно для bignum |
|---|---|---|
| Zero и tiny/small | Fast paths, zero normalization и metadata handling | Позволяет не спутать branch/lifecycle overhead с multiword kernel |
| Medium и large | Linear scaling по числу words | Показывает стоимость traversal и carry/shift propagation |
| `mixed-variable` | Seed-dependent mixed dataset | Проверяет устойчивость adapter к вариативному state без nondeterminism |
| `near-capacity-*` | Почти полная ёмкость state | Выявляет tail handling, boundary loops и copy/preparation premium |
| Kernel-only / end-to-end | Чистая операция против полного mutable lifecycle | Отделяет ASM/C kernel effect от подготовки destination |

> `near-capacity` не означает, что adapter должен вызывать overflow. Adapter обязан сформировать безопасный успешный state для измеряемой операции. Intentional overflow или zeroed status следует проверять отдельным functional test, а не смешивать с performance success path.

## How-to 1. Выполнить полный smoke на CI или локально

Для инфраструктурной проверки используйте короткую нагрузку, но сохраните весь profile set. Это проверяет JSON schema, C11 runner, оба режима и все length/capacity dimensions без production-duration прогона.

```bash
make bench_matrix CONFIG=release \
  REPORT_NAME=full_smoke \
  BENCH_MATRIX_PROFILE=profiles/full.json \
  BENCH_MATRIX_REPETITIONS=1 \
  BENCH_MATRIX_ITERATIONS=1001 \
  BENCH_MATRIX_MT_TOTAL_ITERATIONS=2000 \
  BENCH_MATRIX_WARMUP=10 \
  BENCH_MATRIX_DATA_COUNT=32 \
  MT_THREADS=2 \
  BENCH_MATRIX_TIMEOUT_SECONDS=20
```

## How-to 2. Выполнить production bignum matrix

Ниже приведён complete invocation для bignum adapter. Подберите iteration counts так, чтобы одна группа давала устойчивое время на target host; сохраняйте все параметры в reviewed baseline metadata.

```bash
build/tools/bench_matrix \
  --manifest profiles/full.json \
  --output benchmarks/reports/bignum_full_candidate_matrix.json \
  --st-binary build/bench_bignum_template_st \
  --mt-binary build/bench_bignum_template_mt \
  --repetitions 7 \
  --iterations 200000000 \
  --mt-total-iterations 320000000 \
  --threads 2 \
  --warmup 10000 \
  --data-count 4096 \
  --seed 0x9E3779B97F4A7C15 \
  --timeout-seconds 1800
```

Перед запуском подтвердите, что `320000000 % 2 == 0`. Если система использует SMT, фиксируйте physical-core topology через `taskset` до обеих сторон baseline comparison.

## How-to 3. Создать и использовать full baseline

Сначала агрегируйте reviewed raw matrix. Summary сохраняет median, sample standard deviation и MAD каждой `profile_id × mode` группы и может использоваться непосредственно как baseline.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_full_candidate_matrix.json \
  --output benchmarks/reports/bignum_full_reviewed_summary.json
```

Для candidate compare используйте тот же manifest, иначе C11 gate вернёт exit code `1` из-за missing profile groups.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_full_next_candidate_matrix.json \
  --baseline benchmarks/reports/bignum_full_reviewed_summary.json \
  --output benchmarks/reports/bignum_full_next_candidate_summary.json \
  --threshold-pct 5 \
  --noise-mad-multiplier 3
```

## How-to 4. Разобрать near-capacity result

Сравните `near-capacity-*-kernel` с соответствующим medium/large kernel profile. Если увеличение проявляется только в `end-to-end`, сначала проверьте copy size, dataset layout и allocation/copy boundaries adapter-а. Если оно сохраняется в `kernel-only`, исследуйте длину loop, tail words и branch paths целевой bignum-функции.

## Как редактировать full manifest

Не меняйте ID существующего profile после создания baseline. Чтобы добавить новый bignum-specific scenario, добавьте новый object с уникальным lowercase-hyphen ID и всеми шестью workload fields. Затем создайте новый reviewed baseline: C11 gate намеренно не сопоставляет изменённый group set с прежним artifact.
