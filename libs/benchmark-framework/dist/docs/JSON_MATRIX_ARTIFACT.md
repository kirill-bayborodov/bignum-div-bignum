# JSON format guide: raw matrix artifact

Raw matrix JSON создаётся только C11 executable `build/tools/bench_matrix`. Это отдельный artifact type, а не versioned source manifest: его имя задаётся `--output` или Makefile переменной `MATRIX_REPORT`. Документ описывает каждый generated JSON matrix независимо от имени файла.

## Контракт schema version 1

| Поле | Тип | Назначение |
|---|---|---|
| `schema_version` | integer | Версия artifact contract; текущая версия — `1` |
| `host` | object | `tool`, system/release/machine, logical CPU count и текущая CPU affinity |
| `configuration` | object | repetitions, iteration counts, threads, warmup, data count и seed |
| `profiles` | array | Снимок profiles, фактически принятых executor-ом |
| `samples` | array | Один object для каждого `profile × st|mt × repeat` |
| `failures` | integer | Число samples с non-zero status или invalid protocol |

Каждый sample содержит `profile_id`, `mode`, `repeat_index`, `returncode`, captured `stdout` и либо object `protocol`, либо `protocol_error`. Для valid sample `protocol` содержит как минимум `benchmark`, `elapsed_seconds` и `ns_per_call`.

> Raw matrix не является reviewed baseline сам по себе. Он сохраняет измерения и diagnostics, но decision gate строится только через `benchmark_stats`, который вычисляет median, MAD и остальные metrics.

## How-to 1. Создать raw matrix

```bash
build/tools/bench_matrix \
  --manifest profiles/standard.json \
  --output benchmarks/reports/example_matrix.json \
  --st-binary build/bin/bench_byte_transform \
  --mt-binary build/bin/bench_byte_transform \
  --repetitions 3 --iterations 100000 --mt-total-iterations 200000 \
  --threads 2 --warmup 1000 --data-count 256 \
  --seed 0x9E3779B97F4A7C15 --timeout-seconds 300
```

Команда возвращает `0`, когда все samples вернули success code и строгий protocol. Exit code `1` означает, что JSON всё равно записан, но содержит diagnostics неуспешного sample. Exit code `2` означает invalid CLI, manifest или output I/O, поэтому полагаться на artifact нельзя.

## How-to 2. Найти protocol failure

Откройте JSON и найдите sample без `protocol`, но с `protocol_error`. Проверьте captured `stdout`: успешный adapter обязан напечатать точно одну `benchmark=...` line и затем точно одну `Benchmark finished.` line. Если `returncode` равен `124`, runner остановил child после `--timeout-seconds`.

## How-to 3. Использовать matrix как baseline input

Raw matrix можно передать вторым argument C11 statistics tool. Tool самостоятельно группирует только successful samples по `profile_id × mode`.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/candidate_matrix.json \
  --baseline benchmarks/reports/reviewed_matrix.json \
  --output benchmarks/reports/candidate_summary.json \
  --threshold-pct 5 --noise-mad-multiplier 3
```

Для корректного compare стороны должны иметь одинаковый set profile/mode groups. Если один sample failed и группа потеряла все successful samples, gate остановит pipeline с error.

## How-to 4. Сохранить artifact для воспроизводимости

Храните reviewed raw matrix вместе с summary и source revision adapter-а. Для сравнения фиксируйте profile manifest, seed, thread count, iteration counts, host affinity и compiler configuration. Не сравнивайте raw timing между разными CPU topology или разными workload dimensions как будто это одна группа.
