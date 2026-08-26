# JSON format guide: summary and regression artifact

Summary JSON создаётся C11 executable `build/tools/benchmark_stats`. Artifact получает имя из `--output`; он может быть создан только из candidate matrix либо одновременно с compare against raw matrix/summary baseline. Summary format schema version `1` намеренно пригоден как input следующего запуска statistics tool.

## Контракт schema version 1

| Поле | Тип | Назначение |
|---|---|---|
| `schema_version` | integer | Версия summary contract; текущая версия — `1` |
| `candidate` | string | Путь candidate matrix или summary, переданный через `--input` |
| `baseline` | string/null | Путь optional baseline, переданный через `--baseline` |
| `profiles` | array | Aggregated `profile_id × mode` metrics |
| `comparisons` | array | Candidate/base median comparison для matching groups |
| `missing_profiles` | integer | Число groups, присутствующих только на одной стороне |
| `regressions` | integer | Число confirmed regressions |

Каждый object `profiles` содержит `profile_id`, `mode` и object `metrics`. Metrics включают `sample_count`, `minimum_ns_per_call`, `maximum_ns_per_call`, `mean_ns_per_call`, `median_ns_per_call`, `stdev_ns_per_call` и `mad_ns_per_call`.

Каждый object `comparisons` сохраняет candidate/baseline medians, `relative_delta_pct`, policy `threshold_pct`, calculated `noise_floor_pct` и boolean `regression`.

> Regression подтверждается только когда candidate median медленнее baseline и одновременно превышает оба барьера: configured percentage threshold и `noise_mad_multiplier × baseline MAD` относительно baseline median.

## How-to 1. Агрегировать raw matrix без baseline

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_matrix.json \
  --output benchmarks/reports/bignum_matrix_summary.json
```

Команда возвращает `0` и создаёт profiles metrics. Используйте summary как кандидат для review; не называйте его baseline, пока не подтвердили стабильность host, manifest и toolchain.

## How-to 2. Проверить candidate против reviewed summary

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/bignum_candidate_matrix.json \
  --baseline benchmarks/reports/bignum_reviewed_summary.json \
  --output benchmarks/reports/bignum_candidate_summary.json \
  --threshold-pct 5 \
  --noise-mad-multiplier 3
```

Exit code `0` означает matching profile groups и отсутствие confirmed regressions. Exit code `1` означает missing group или regression; JSON summary всё равно создаётся для review. Exit code `2` означает invalid input/CLI or output I/O.

## How-to 3. Разрешить exploratory regression, сохранив запись

Используйте `--allow-regressions`, когда требуется собрать summary от заведомо медленного experiment, но CI exit code не должен блокировать exploratory task. Поле `regressions` и boolean каждой comparison всё равно сохраняются в output.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/experiment_matrix.json \
  --baseline benchmarks/reports/reviewed_summary.json \
  --output benchmarks/reports/experiment_summary.json \
  --threshold-pct 5 \
  --allow-regressions
```

## How-to 4. Интерпретировать один slow group

Сначала сравните `median_ns_per_call`, затем `mad_ns_per_call`. Если relative delta ниже noise floor, tool не отмечает regression: разброс baseline недостаточно мал для статистически полезного вывода. Если regression подтверждён только в `end-to-end`, исследуйте preparation/copy mutable state; если он подтверждён также в `kernel-only`, исследуйте фактический C/ASM operation path.

## How-to 5. Использовать summary как новый baseline input

Summary уже содержит полный metrics contract, поэтому повторный C11 tool не требует raw samples.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/next_candidate_matrix.json \
  --baseline benchmarks/reports/bignum_reviewed_summary.json \
  --output benchmarks/reports/next_candidate_summary.json
```

Не редактируйте numbers вручную. Создавайте новый baseline только из reviewed run с неизменными profile IDs, manifest semantics, CPU affinity, threads and toolchain.
