# How-to: профилирование bignum-модулей через benchmark-framework

Это руководство показывает, как подключать **benchmark-core** и **benchmark-framework** к библиотекам семейства bignum, не помещая bignum-зависимости внутрь framework. Каждая измеряемая операция остаётся собственностью целевого репозитория; framework поставляет только lifecycle dataset, warm-up, ST/MT запуск, JSON matrix и статистическую проверку.

> **Главное правило.** Callback `initialize` создаёт immutable source-state. Перед каждым измеренным вызовом core создаёт независимую mutable копию этого record. Поэтому in-place операции `bignum_shift_right`, `bignum_template` и `bignum_sub_bignum` не портят следующий sample и могут измеряться в MT без общего mutable state.

## How-to 1. Добавить framework и bignum-зависимости

В проекте конкретной операции добавьте четыре независимых submodule. Укажите фактические URLs bignum-репозиториев, используемые вашим проектом.

```bash
git submodule add https://github.com/kirill-bayborodov/benchmark-core.git libs/benchmark-core
git submodule add https://github.com/kirill-bayborodov/benchmark-framework.git libs/benchmark-framework
git submodule add https://github.com/kirill-bayborodov/bignum-core.git libs/bignum-core
git submodule add https://github.com/kirill-bayborodov/bignum-shift-right.git libs/bignum-shift-right
git submodule update --init --recursive
```

Проекту нужен только `benchmark-core` для своего adapter binary. `benchmark-framework` можно использовать как шаблон Makefile-целей, manifests, C11 tools и CI. Не добавляйте bignum-зависимости в сам framework: это сохраняет его нейтральным для иных библиотек.

## How-to 2. Добавить bignum shift-right adapter

Ниже приведён **полный** single-source adapter. Он измеряет `bignum_shift_right` и поддерживает стандартные workload-оси `input_kind`, `operation_kind`, `measure_mode`, `size_profile` и `capacity_profile`. Статус `BIGNUM_SHIFT_RIGHT_ZEROED` намеренно не считается успешным: profile generator выбирает длину не меньше двух words и сдвиги, которые не обнуляют число за один измеренный вызов.

Создайте `benchmarks/bench_bignum_shift_right.c`.

```c
#include <benchmark_core.h>
#include <bignum.h>
#include <bignum_shift_right.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bignum_t value;
    size_t shift;
} shift_right_state_t;

static uint64_t mix64(uint64_t value)
{
    value += UINT64_C(0x9E3779B97F4A7C15);
    value = (value ^ (value >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31U);
}

static size_t words_for_profile(const benchmark_workload_t *workload)
{
    if (strcmp(workload->capacity_profile, "near-capacity") == 0) {
        return BIGNUM_CAPACITY - 1U;
    }
    if (strcmp(workload->size_profile, "tiny") == 0) return 2U;
    if (strcmp(workload->size_profile, "small") == 0) return 4U;
    if (strcmp(workload->size_profile, "medium") == 0) return BIGNUM_CAPACITY / 2U;
    if (strcmp(workload->size_profile, "large") == 0) return BIGNUM_CAPACITY - 2U;
    if (strcmp(workload->size_profile, "variable") == 0) {
        return 2U + (size_t)(workload->seed % (BIGNUM_CAPACITY - 2U));
    }
    return 4U;
}

static size_t shift_for_profile(const benchmark_workload_t *workload)
{
    if (strcmp(workload->operation_kind, "zero") == 0) return 0U;
    if (strcmp(workload->operation_kind, "word") == 0) return 64U;
    if (strcmp(workload->operation_kind, "bit") == 0) return 13U;
    if (strcmp(workload->operation_kind, "combined") == 0) return 77U;
    return 13U;
}

static int initialize(void *record, uint64_t sequence_index,
    const benchmark_workload_t *workload, void *context)
{
    shift_right_state_t *state = record;
    uint64_t seed = workload->seed ^ sequence_index;
    const size_t words = words_for_profile(workload);
    (void)context;

    memset(state, 0, sizeof(*state));
    state->shift = shift_for_profile(workload);
    if (strcmp(workload->input_kind, "zero") == 0) return BIGNUM_SHIFT_RIGHT_SUCCESS;

    state->value.len = words;
    for (size_t index = 0U; index < words; ++index) {
        state->value.words[index] = mix64(seed + index);
    }
    state->value.words[words - 1U] |= UINT64_C(1);
    return BIGNUM_SHIFT_RIGHT_SUCCESS;
}

static int operation(void *record, uint64_t iteration,
    const benchmark_workload_t *workload, void *context)
{
    shift_right_state_t *state = record;
    (void)iteration;
    (void)workload;
    (void)context;
    return bignum_shift_right(&state->value, state->shift) == BIGNUM_SHIFT_RIGHT_SUCCESS
        ? BIGNUM_SHIFT_RIGHT_SUCCESS
        : BIGNUM_SHIFT_RIGHT_ERROR_NULL_ARG;
}

static uint64_t checksum(const void *record, uint64_t iteration, void *context)
{
    const shift_right_state_t *state = record;
    uint64_t value = (uint64_t)state->value.len ^ iteration;
    (void)context;
    for (size_t index = 0U; index < state->value.len; ++index) {
        value ^= state->value.words[index] + UINT64_C(0x9E3779B97F4A7C15);
        value = (value << 7U) | (value >> 57U);
    }
    return value;
}

int main(int argc, char **argv)
{
    const benchmark_adapter_t adapter = {
        .benchmark_name = "bignum_shift_right",
        .state_size = sizeof(shift_right_state_t),
        .success_code = BIGNUM_SHIFT_RIGHT_SUCCESS,
        .adapter_context = NULL,
        .initialize = initialize,
        .operation = operation,
        .checksum = checksum
    };
#ifdef BENCHMARK_MODE_MT
    return benchmark_core_run_mt(argc, argv, &adapter);
#else
    return benchmark_core_run_st(argc, argv, &adapter);
#endif
}
```

Соберите два binary из одного source. Первый запускает ST runner, второй — MT runner.

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
  -Ilibs/benchmark-core/include -Ilibs/bignum-core/include -Ilibs/bignum-shift-right/include \
  benchmarks/bench_bignum_shift_right.c \
  libs/benchmark-core/build/libbenchmark_core.a \
  libs/bignum-shift-right/dist/lib/libbignum_shift_right.a \
  -pthread -o build/bench_bignum_shift_right_st

gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic -DBENCHMARK_MODE_MT \
  -Ilibs/benchmark-core/include -Ilibs/bignum-core/include -Ilibs/bignum-shift-right/include \
  benchmarks/bench_bignum_shift_right.c \
  libs/benchmark-core/build/libbenchmark_core.a \
  libs/bignum-shift-right/dist/lib/libbignum_shift_right.a \
  -pthread -o build/bench_bignum_shift_right_mt
```

Если конкретный bignum-релиз называет archive иначе, измените **только** путь к archive в команде линковки. Контракт adapter и параметры runner не меняются.

## How-to 3. Выполнить один воспроизводимый ST или MT запуск

Нулевой shift изолирует lifecycle и копирование state. `bit`, `word` и `combined` разделяют соответствующие пути операции. Профиль `near-capacity` создаёт значение длиной `BIGNUM_CAPACITY - 1` words и отделяет capacity-эффект от коротких operands.

```bash
build/bench_bignum_shift_right_st \
  --iterations 5000000 --warmup 10000 --data-count 4096 --seed 0x9E3779B97F4A7C15 \
  --input-kind nonzero --operation-kind combined --measure-mode kernel-only \
  --size-profile medium --capacity-profile normal

build/bench_bignum_shift_right_mt \
  --threads 2 --total-iterations 10000000 --warmup 10000 --data-count 4096 \
  --seed 0x9E3779B97F4A7C15 --input-kind nonzero --operation-kind bit \
  --measure-mode end-to-end --size-profile near-capacity --capacity-profile near-capacity
```

Для MT `total-iterations` обязательно делится на `threads`. Сравнение MT результатов корректно только при зафиксированной topology: например, `taskset -c 0,2` выбирает разные physical cores в текущем стенде, а `taskset -c 0,1` может выбрать SMT siblings.

## How-to 4. Описать bignum matrix manifest

Создайте `profiles/bignum-shift-right.json`. Поля workload передаются adapter без переинтерпретации core; поэтому названия `operation_kind` может использовать и `bignum_template`, и другой модуль, если adapter документирует их смысл.

```json
{
  "schema_version": 1,
  "description": "bignum_shift_right: path, length and near-capacity matrix",
  "profiles": [
    {
      "id": "zero-tiny-zero-kernel",
      "input_kind": "zero",
      "operation_kind": "zero",
      "measure_mode": "kernel-only",
      "size_profile": "tiny",
      "capacity_profile": "normal"
    },
    {
      "id": "nonzero-small-bit-kernel",
      "input_kind": "nonzero",
      "operation_kind": "bit",
      "measure_mode": "kernel-only",
      "size_profile": "small",
      "capacity_profile": "normal"
    },
    {
      "id": "nonzero-medium-word-kernel",
      "input_kind": "nonzero",
      "operation_kind": "word",
      "measure_mode": "kernel-only",
      "size_profile": "medium",
      "capacity_profile": "normal"
    },
    {
      "id": "nonzero-large-combined-e2e",
      "input_kind": "nonzero",
      "operation_kind": "combined",
      "measure_mode": "end-to-end",
      "size_profile": "large",
      "capacity_profile": "normal"
    },
    {
      "id": "nonzero-near-capacity-combined-kernel",
      "input_kind": "nonzero",
      "operation_kind": "combined",
      "measure_mode": "kernel-only",
      "size_profile": "near-capacity",
      "capacity_profile": "near-capacity"
    }
  ]
}
```

## How-to 5. Выполнить matrix C11 tool и сохранить JSON

`bench_matrix` — скомпилированный C11 executable. Он запускает каждый `profile × mode × repetition` через `fork`/`execv`, проверяет порядок `benchmark=...` затем `Benchmark finished.`, сохраняет stdout и parsed timing в JSON, и не вызывает Python или shell interpolation.

```bash
build/tools/bench_matrix \
  --manifest profiles/bignum-shift-right.json \
  --output benchmarks/reports/shift_right_candidate_matrix.json \
  --st-binary build/bench_bignum_shift_right_st \
  --mt-binary build/bench_bignum_shift_right_mt \
  --repetitions 7 --iterations 5000000 --mt-total-iterations 10000000 --threads 2 \
  --warmup 10000 --data-count 4096 --seed 0x9E3779B97F4A7C15 --timeout-seconds 1800
```

## How-to 6. Агрегировать метрики и остановить регрессию

`benchmark_stats` также является C11 executable. Для каждой группы `profile_id × mode` он рассчитывает minimum, maximum, mean, median, sample stdev и MAD. Кандидат признаётся regression, только если его median медленнее baseline **и** одновременно превышает percentage threshold, и noise floor `MAD multiplier × baseline MAD`.

```bash
build/tools/benchmark_stats \
  --input benchmarks/reports/shift_right_candidate_matrix.json \
  --output benchmarks/reports/shift_right_candidate_summary.json \
  --baseline benchmarks/reports/shift_right_reviewed_baseline.json \
  --threshold-pct 5 \
  --noise-mad-multiplier 3
```

Команда возвращает `0`, когда group set совпадает и подтверждённых регрессий нет; `1`, когда отсутствует группа либо обнаружена регрессия; `2`, когда JSON или CLI некорректен. Для exploratory-run, который всё равно должен создать summary, добавьте `--allow-regressions`.

## How-to 7. Перенести тот же паттерн на bignum_template

У `bignum_template` сигнатура `bignum_template(bignum_t *num, size_t template_amount)` и success-code `BIGNUM_TEMPLATE_SUCCESS`. Скопируйте adapter из How-to 2 и внесите только три предметных замены: include, callback operation и success code.

```c
#include <bignum_template.h>

static int operation(void *record, uint64_t iteration,
    const benchmark_workload_t *workload, void *context)
{
    shift_right_state_t *state = record;
    (void)iteration;
    (void)workload;
    (void)context;
    return bignum_template(&state->value, state->shift) == BIGNUM_TEMPLATE_SUCCESS
        ? BIGNUM_TEMPLATE_SUCCESS
        : BIGNUM_TEMPLATE_ERROR_NULL_ARG;
}
```

В object `benchmark_adapter_t` используйте `.benchmark_name = "bignum_template"` и `.success_code = BIGNUM_TEMPLATE_SUCCESS`. Для left-shift операции near-capacity profile должен быть **предварительно безопасен от overflow**: оставьте достаточно нулевых старших words либо интерпретируйте ожидаемый `BIGNUM_TEMPLATE_ERROR_OVERFLOW` как отдельный non-performance test, но не смешивайте ошибочный путь с measurements success path.

## How-to 8. Добавить Makefile-цель целевого bignum проекта

Ниже приведён самостоятельный recipe, который можно добавить в Makefile конкретного bignum проекта. Он не требует Python.

```make
BENCH_FRAMEWORK_DIR := libs/benchmark-framework
BENCH_MATRIX := $(BENCH_FRAMEWORK_DIR)/build/tools/bench_matrix
BENCH_STATS := $(BENCH_FRAMEWORK_DIR)/build/tools/benchmark_stats
BENCH_MANIFEST := profiles/bignum-shift-right.json
BENCH_REPORT := benchmarks/reports/shift_right_matrix.json
BENCH_SUMMARY := benchmarks/reports/shift_right_summary.json

bench_matrix: build
	@$(BENCH_MATRIX) --manifest $(BENCH_MANIFEST) --output $(BENCH_REPORT) \
		--st-binary build/bench_bignum_shift_right_st \
		--mt-binary build/bench_bignum_shift_right_mt \
		--repetitions 7 --iterations 5000000 --mt-total-iterations 10000000 \
		--threads 2 --warmup 10000 --data-count 4096 --seed 0x9E3779B97F4A7C15 \
		--timeout-seconds 1800
	@$(BENCH_STATS) --input $(BENCH_REPORT) --output $(BENCH_SUMMARY) \
		--threshold-pct 5 $(if $(BENCH_BASELINE),--baseline $(BENCH_BASELINE))
```

Перед включением recipe в защищённый Makefile bignum-template требуется отдельное согласование владельца. Для smoke проверки переопределяйте workload variables или используйте короткий manifest; не уменьшайте константы production Makefile неявно.

## How-to 9. Читать результаты правильно

Сравнивайте только совпадающие `profile_id × mode`, один CPU topology и одинаковые toolchain/configuration. `kernel-only` отвечает на вопрос о цене самой операции на уже подготовленном mutable state. `end-to-end` включает copy/preparation, поэтому разница между ними показывает preparation premium, но не является автоматически ASM regression.

При недоступности hardware PMU events используйте runtime matrix и программные perf events (`task-clock`, `context-switches`, `cpu-migrations`, `page-faults`). Не интерпретируйте отсутствие `cache-misses:u` или `cycles:u` в VM как ухудшение кода: это ограничение PMU virtualisation среды.
