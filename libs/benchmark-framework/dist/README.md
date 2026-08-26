# benchmark-framework

`benchmark-framework` — самостоятельная C11-библиотека и шаблонный репозиторий для семейства `benchmark-lib`. Она предназначена для воспроизводимого измерения in-place операций из произвольных доменов: числовых библиотек, буферных преобразований, кодеков, криптографических примитивов и других компонентов с контролируемым mutable state.

Framework не зависит от `bignum_t`, конкретной операции или bignum-репозиториев. Потребляющий проект подключает ядро через небольшой adapter, а framework обеспечивает общий lifecycle данных, ST/MT timing, protocol, матричный прогон и статистическую проверку регрессий.

## Архитектура

| Компонент | Путь | Роль |
|---|---|---|
| Ядро (Git submodule) | `libs/benchmark-core` | Domain-neutral C11 runner, adapter API, dataset lifecycle, protocol, ST/MT orchestration |
| Matrix tooling | `tools/bench_matrix.c` | C11 executable: выполняет manifest-профили, запускает adapters через `fork`/`execv` и сохраняет raw samples в JSON |
| Statistics | `tools/benchmark_stats.c` | C11 executable: считает median/mean/stdev/MAD и проверяет candidate против reviewed baseline |
| JSON library (Git submodule) | `libs/json-lib` | Закреплённый `v1.0.2` status-only C11 JSON parser/writer для manifests, raw matrix и summary artifacts |
| Manifests | `profiles/` | Рассмотренные domain-neutral standard и full наборы workload-профилей |
| Example adapter | `examples/byte-transform/` | Независимый byte-buffer adapter для проверки интеграции и как образец для клиентских библиотек |

`libs/benchmark-core` и `libs/json-lib` — настоящие Git submodules, закреплённые на рассмотренных commits самостоятельных C11-библиотек. Framework использует только public `json_lib.h` и `libjson_lib.a`; temporary internal JSON parser не поставляется. Framework не содержит bignum-кода или bignum-dependencies: конкретный bignum adapter и его manifests принадлежат потребляющему репозиторию.

Подробная граница слоёв описана в [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), а практическое подключение bignum-модулей — в [docs/HOW_TO_BIGNUM.md](docs/HOW_TO_BIGNUM.md). Отдельные форматы generated artifacts документированы в [docs/JSON_MATRIX_ARTIFACT.md](docs/JSON_MATRIX_ARTIFACT.md) и [docs/JSON_SUMMARY_ARTIFACT.md](docs/JSON_SUMMARY_ARTIFACT.md).

## Контракт успешного benchmark-а

Каждая успешная программа обязана напечатать ровно одну машинно-читаемую строку, за которой следует ровно один маркер завершения.

```text
benchmark=<stable-name>_st|mt ... elapsed_seconds=<seconds> ns_per_call=<nanoseconds>
Benchmark finished.
```

> Строка `Benchmark finished.` должна идти **после** `benchmark=...`. Matrix-runner отвергает результат, нарушающий это правило.

Protocol включает workload metadata (`data_mode`, `input_kind`, `operation_kind`, `measure_mode`, `size_profile`, `capacity_profile`), параметры воспроизводимости (`seed`, `warmup`, `data_count`), fingerprint исходного dataset, checksum, успешные вызовы, duration и `ns_per_call`.

## Adapter API

Клиент реализует `benchmark_adapter_t` из `libs/benchmark-core/include/benchmark_core.h`. Adapter задаёт размер одного state record и три callback:

| Callback | Ответственность |
|---|---|
| `initialize` | Детерминированно создаёт один immutable source-state по `seed`, sequence index и workload metadata |
| `operation` | Выполняет измеряемую in-place операцию над отдельной mutable копией state |
| `checksum` | Извлекает детерминированное наблюдаемое значение из post-operation state |

Core передаёт все поля workload в adapter, но не приписывает им предметный смысл. Так, `operation_kind=rotate` может означать byte rotation в example-adapter, сдвиг в bignum-библиотеке или иную операцию у другого потребителя.

## Workload-профили

| Ось | CLI | ENV | Пример значения |
|---|---|---|---|
| Тип входа | `--input-kind` | `BENCH_INPUT_KIND` | `zero`, `nonzero`, `mixed` |
| Вид операции | `--operation-kind` | `BENCH_OPERATION_KIND` | `noop`, `xor`, `rotate` |
| Граница времени | `--measure-mode` | `BENCH_MEASURE_MODE` | `end-to-end`, `kernel-only` |
| Размер state | `--size-profile` | `BENCH_SIZE_PROFILE` | `tiny`, `small`, `medium`, `large`, `near-capacity` |
| Профиль capacity | `--capacity-profile` | `BENCH_CAPACITY_PROFILE` | `normal`, `near-capacity` |

Также доступны `--warmup`, `--data-count`, `--seed`; для ST — `--iterations`, для MT — `--threads` и `--total-iterations`. MT total iterations обязано делиться на число потоков. Legacy `--data-mode all_zero|all_nonzero|mixed` сохранён как компактная совместимая запись базовых сочетаний.

`near-capacity` — метаданные профиля, а не навязанная core семантика. Adapter обязан явно определить безопасный near-capacity state и правила успешности, не смешивая normal success path с intentional error path.

## Сборка и тесты

```bash
make build CONFIG=release
make test
make lint
make test_sanitize
make test_helgrind
```

`make test` проверяет core, json-lib dependency build и ST/MT protocol нейтрального `byte-transform` adapter. `make test_sanitize` использует AddressSanitizer и UndefinedBehaviorSanitizer. `make test_helgrind` начинает с чистой normal build, поэтому не наследует sanitizer runtime от предшествующей проверки, и проверяет многопоточный example под Helgrind.

## JSON matrix и baseline regression gate

Полная матрица профилей запускается без зависимости от аппаратных PMU events.

```bash
make bench_matrix CONFIG=release \
  REPORT_NAME=baseline \
  BENCH_MATRIX_REPETITIONS=7 \
  BENCH_MATRIX_ITERATIONS=200000000 \
  BENCH_MATRIX_MT_TOTAL_ITERATIONS=320000000 \
  MT_THREADS=2
```

Результат состоит из двух JSON-артефактов в `benchmarks/reports/`:

| Артефакт | Содержимое |
|---|---|
| `<report>_matrix.json` | Host metadata, configuration, profiles, stdout, exit status и разобранные protocol timing fields каждого sample |
| `<report>_matrix_summary.json` | Median, mean, sample standard deviation, MAD и результат baseline comparison для каждой пары `profile_id` + `st|mt` |

Чтобы проверить candidate против рассмотренного baseline, передайте baseline явно. Сравнение отвергает несовпадающий набор profile ID и выявляет регрессию, когда candidate median `ns_per_call` выше заданного порога **и** выше MAD noise floor baseline.

```bash
make bench_matrix CONFIG=release \
  REPORT_NAME=candidate \
  BENCH_BASELINE=benchmarks/reports/baseline_matrix.json \
  BENCH_REGRESSION_THRESHOLD_PCT=5
```

Baseline не должен заменяться автоматически. Его необходимо создавать из рассмотренного прогона с зафиксированными host metadata, compiler/toolchain, CPU affinity, manifest hash, seed, thread count и iteration counts.

Для каждого versioned manifest предусмотрен отдельный companion how-to: [profiles/standard.json.md](profiles/standard.json.md) описывает быстрый smoke profile set, а [profiles/full.json.md](profiles/full.json.md) — полный length/near-capacity regression set.

Для короткого smoke-прогона выберите `profiles/standard.json`:

```bash
make bench_matrix \
  BENCH_MATRIX_PROFILE=profiles/standard.json \
  BENCH_MATRIX_REPETITIONS=1 \
  BENCH_MATRIX_ITERATIONS=1000 \
  BENCH_MATRIX_MT_TOTAL_ITERATIONS=2000
```

## Практические bignum-примеры

[docs/HOW_TO_BIGNUM.md](docs/HOW_TO_BIGNUM.md) содержит complete adapter для `bignum_shift_right`, manifest c `bit`/`word`/`combined`, `near-capacity` и `kernel-only` сценариями, команды ST/MT, C11 matrix/statistics workflow, baseline gate и безопасный перенос паттерна на `bignum_template`.

## Использование как шаблона

Новый проект семейства `benchmark-lib` должен сохранить разделение ответственности: проектный adapter и его domain-specific manifests остаются у потребителя, а общий lifecycle остаётся в `benchmark-core`. Подключайте `benchmark-core` и `json-lib` через Git submodules и храните закреплённые commit hashes вместе с изменениями adapter-а, чтобы benchmarks и JSON artifacts были воспроизводимы.

## License

Проект распространяется по лицензии MIT. См. [LICENSE](LICENSE).
