# Архитектура benchmark-framework

## Назначение

`benchmark-framework` является самостоятельной библиотекой и шаблонным репозиторием для семейства `benchmark-lib`. Он не зависит от `bignum_t`, конкретной арифметической операции или внешних функциональных библиотек. Потребляющий проект подключает framework через небольшой adapter и сохраняет у себя только domain-specific создание данных и вызов измеряемой функции.

## Слои

| Слой | Местоположение | Ответственность |
|---|---|---|
| `benchmark-core` | `libs/benchmark-core` | C11 API adapter-ов, ST/MT runner, timing, обязательный protocol, CLI и ENV parsing, lifecycle thread workers |
| `json-lib` | `libs/json-lib` | Закреплённый status-only C11 parser/writer для manifests, raw matrix и summary artifacts |
| Framework tooling | `tools/` | C11 JSON matrix-runner, bounded process execution, host metadata, protocol validation, статистическая агрегация и baseline regression gate |
| Workload manifests | `profiles/` | Рассмотренные наборы workload-профилей, независимые от конкретной предметной модели |
| Example adapter | `examples/byte-transform/` | Нейтральный пример in-place byte-buffer операции и smoke-тест интеграции |
| Integration contract | `include/`, `benchmarks/` | Заголовки и документированный шаблон подключения benchmark-core в потребляющую библиотеку |

## benchmark-core

`benchmark-core` подключён как настоящий Git submodule в `libs/benchmark-core`. Framework закрепляет конкретный рассмотренный commit core; обновление gitlink выполняется отдельным изменением framework, чтобы воспроизводимость adapter-ов и matrix results не зависела от плавающей ветки.

Ядро принимает `benchmark_adapter_t`, который задаёт стабильное имя benchmark, callbacks lifecycle и операции, пользовательский context, размер mutable state и код успеха. Adapter отвечает за смысл workload-полей; core отвечает за их детерминированную передачу, timing, warm-up, ST/MT orchestration и протокол вывода.

> Успешный runner печатает ровно одну строку `benchmark=...` и затем ровно одну строку `Benchmark finished.`. Этот порядок является частью межпроектного контракта.

### Контракт результатов операций

Все fallible API `benchmark-core` возвращают `benchmark_core_status_t` либо `benchmark_adapter_status_t`; callbacks adapter-а не используют неименованные `int` коды. JSON library возвращает `json_status_t`, а executables matrix/statistics используют соответственно `bench_matrix_status_t` и `benchmark_stats_status_t`. Именованные boolean-предикаты (`benchmark_boolean_t`, `json_boolean_t` и tool-level аналоги) отделяют условные результаты от ошибок. Значение индекса или числовой метрики передаётся через typed output parameter, поэтому ни `-1`, ни `0` не несут скрытый error status. Только hosted ISO C функция `main` явно отображает внутренний status в `int` exit code для shell и Makefile boundary.

## Workload и результат

Manifest описывает профили нейтральными строковыми полями: `input_kind`, `operation_kind`, `measure_mode`, `size_profile` и `capacity_profile`. Эти значения включаются в machine-readable protocol и JSON без интерпретации core. Потребляющая библиотека может использовать набор значений, соответствующий её предметной области, сохраняя воспроизводимость по `seed`, `warmup` и `data_count`.

`tools/bench_matrix.c` компилируется в C11 executable и запускает ровно объявленные профили через сформированный `argv` с `fork`/`execv`, проверяет protocol и сохраняет raw samples в JSON. `tools/benchmark_stats.c` группирует успешные samples по `profile_id` и режиму (`st` или `mt`) и сравнивает candidate с reviewed baseline посредством median, sample standard deviation и MAD. Закреплённый `libs/json-lib` строго читает manifests и artifacts через public status-only API, а JSON output создаётся через `json_writer_t` temporary transaction и atomic rename.

## Не включается в framework

Из `benchmark-framework` удаляются bignum-реализация, bignum tests, bignum-specific workload generator и submodule `bignum-core`, `bignum-shift-right`, `bignum-sub-bignum`. Они относятся к потребляющим библиотекам. Тонкий adapter для `bignum_template` остаётся в соответствующем проекте и подключает `benchmark-core` через submodule. Полные практические примеры для `bignum_shift_right` и `bignum_template` приведены в [HOW_TO_BIGNUM.md](HOW_TO_BIGNUM.md).

## Обязательный стандарт документации

Каждый C11 header, type, function и самостоятельный алгоритмический блок должен иметь Doxygen-комментарий. `@brief` формулирует одно краткое назначение, а `@details` объясняет алгоритм, границы данных, ownership, потоковую модель и error path. Ключевые участки исполнения дополняются inline-комментариями: lifecycle immutable/mutable state, timing boundary, process/thread synchronization, atomic JSON publication и regression decision.

| Артефакт | Обязательная companion-документация |
|---|---|
| `profiles/standard.json` | [`../profiles/standard.json.md`](../profiles/standard.json.md): profile semantics, smoke, bignum adapter и baseline how-to |
| `profiles/full.json` | [`../profiles/full.json.md`](../profiles/full.json.md): length/near-capacity matrix и production regression how-to |
| Raw matrix JSON | [JSON_MATRIX_ARTIFACT.md](JSON_MATRIX_ARTIFACT.md): schema, diagnostics и lifecycle artifact |
| Summary JSON | [JSON_SUMMARY_ARTIFACT.md](JSON_SUMMARY_ARTIFACT.md): metrics, baseline policy и interpretation |

Новый JSON source или generated JSON artifact class не считается завершённым, пока рядом не существует отдельный Markdown how-to с назначением, field contract и пошаговыми командами создания, проверки и безопасного использования. Distribution `make dist` обязан включать JSON file и его companion guide.
