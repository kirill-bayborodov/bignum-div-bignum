# Quality Gates для документирования C11-кода и JSON артефактов

**Статус:** проект единого нормативного стандарта.  
**Область применения:** библиотеки и benchmark-проекты семейства `bignum-lib`, включая submodules, тесты, C11-инструменты, C/ASM boundary code, JSON manifests и сопроводительные руководства.  
**Нормативные слова:** **ДОЛЖЕН**, **НЕ ДОЛЖЕН**, **ОБЯЗАН** означают блокирующее требование; **СЛЕДУЕТ** означает обязательный предмет review, для которого допустимо явно задокументированное исключение; **МОЖЕТ** означает необязательное улучшение.

> **Критерий качества документации:** читатель, не участвовавший в реализации, должен суметь понять назначение, контракт, алгоритм, допустимые состояния, владение ресурсами и способ проверки артефакта без чтения его исходной реализации.

## 1. Назначение и границы стандарта

Этот документ устанавливает минимально допустимый уровень документации, при котором C11-код, JSON-конфигурации и benchmark-сценарии могут быть приняты в основной branch. Стандарт распространяется на public и internal code: внутренний характер функции не отменяет необходимости объяснить её алгоритм, ограничения и роль в инварианте модуля.

Документация не заменяет проверку кода, тесты, static analysis или dynamic analysis. Она описывает **контракт**, который затем должен быть проверяем кодом и тестами. Если документация и реализация расходятся, это является дефектом уровня quality gate.

| Артефакт | Обязательный набор документации | Минимальный способ проверки |
|---|---|---|
| Public C header | File Doxygen, type/enum/function Doxygen, ownership и status contract | Doxygen без warnings; review public API |
| Internal C source | File Doxygen, Doxygen для internal/static functions, inline explanation сложных blocks | Manual review + source documentation audit |
| C entry point | File Doxygen, `main` usage/exit contract, protocol description | Build/run smoke test |
| Complex type | Type Doxygen и inline comment **каждого поля** | Field-by-field review |
| Status enum | Type Doxygen и inline comment **каждого code** | API/status review |
| Test source | File/test-case Doxygen, scenario, oracle и expected status | Deterministic test execution |
| Benchmark profile JSON | Adjacent `*.json.md` how-to document | JSON parse + documented command |
| README/Usage Guide | Build, integration, minimal example, cleanup/troubleshooting | Example compiles/runs unchanged |
| C/ASM boundary | ABI, register/stack/clobber, data representation, error semantics | Build + ABI-facing tests/review |

## 2. Общие Quality Gates

### QG-DOC-001 — Документация должна быть синхронна с кодом

Каждая изменённая публичная сигнатура, перечисление, поле структуры, JSON field, CLI argument, Make target или observable output protocol **ДОЛЖНЫ** одновременно получить соответствующее обновление документации. Нельзя принимать изменение, которое оставляет устаревшую команду, неверный return contract, удалённый profile identifier или несуществующий файл в README.

**Критерий приёмки:** reviewer сопоставляет diff реализации с diff документации и не находит необъяснённых contract changes. Все приведённые команды запускаются с копированием без ручной коррекции.

### QG-DOC-002 — Документация описывает контракт, а не пересказывает синтаксис

Запрещена формальная документация вида «функция делает X» без условий. Документируемый контракт **ДОЛЖЕН** отвечать минимум на вопросы: что делает сущность, на каких входах, что она не делает, как интерпретируется результат, кто владеет ресурсом, что происходит при ошибке и потокобезопасна ли операция.

### QG-DOC-003 — Документация должна быть локальной и проверяемой

Существенное правило размещается рядом с объектом, к которому относится. API contract размещается в header; инвариант локальной переменной — inline возле блока; использование JSON manifest — в companion document; end-to-end integration — в README или Usage Guide. Ссылка на удалённый документ не заменяет локального описания критического условия.

### QG-DOC-004 — Обязательная терминология

В пределах одного модуля одинаковое понятие именуется одинаково. Например, нельзя смешивать `status`, `error code`, `return value` для одного `*_status_t`, либо `word length`, `operand size`, `capacity` без определения различий. Новый термин при первом употреблении **ДОЛЖЕН** быть определён.

## 3. Doxygen Quality Gates для заголовков

### QG-HDR-001 — File-level Doxygen

Каждый `.h` **ДОЛЖЕН** начинаться Doxygen-блоком, содержащим:

| Поле | Содержание |
|---|---|
| `@file` | Canonical file name |
| `@brief` | Однострочное назначение header |
| `@details` | Модульная граница, ключевой алгоритм/модель и ограничения |
| Public contract | Ownership, allocation model, thread-safety или ссылка на соответствующий section |
| Dependencies | Только если порядок/роль зависимости неочевидны |

`@brief` **ДОЛЖЕН** оставаться одной краткой строкой. Алгоритм, rationale, ограничения и примеры размещаются в `@details`, а не в разросшемся `@brief`.

### QG-HDR-002 — Public types и enumerations

Каждый public `typedef`, `struct`, `union`, `enum` и callback type **ДОЛЖЕН** иметь Doxygen-блок непосредственно перед объявлением. Описание обязано раскрывать назначение, допустимое время жизни и связь с другими типами.

Для `struct`/`union` обязательны оба уровня описания:

1. Doxygen-блок типа объясняет целостный смысл объекта, ownership, mutability, state lifecycle и invariants.
2. **Каждое поле** имеет inline-comment, описывающий единицы измерения, допустимый диапазон, `NULL` semantics, ownership и момент валидности.

```c
/**
 * @brief Describes one benchmark invocation accepted by the adapter.
 * @details The structure is immutable after validation. The adapter reads all
 * fields before worker creation, so callers retain ownership of string storage
 * until the run status has been returned.
 */
typedef struct benchmark_request {
    const char *profile_id; /**< [in] Stable JSON profile identifier; non-NULL. */
    size_t iterations;      /**< [in] Positive invocation count per measured run. */
    size_t worker_count;    /**< [in] MT worker count; exactly one for ST mode. */
} benchmark_request_t;
```

### QG-HDR-003 — Named statuses

Каждый `*_status_t` **ДОЛЖЕН** быть именованным `enum` type с Doxygen-блоком, поясняющим общий status model. Каждый enumerator **ДОЛЖЕН** иметь inline-comment, описывающий:

- точную ситуацию, в которой он возвращается;
- состояние output parameters при этом return code;
- допустимость повторной попытки, если это существенно;
- отличие от соседнего status code.

```c
/**
 * @brief Reports the result of adapter validation or benchmark preparation.
 * @details No function in this module returns a primitive status value. A
 * successful status guarantees all documented output parameters were written.
 */
typedef enum adapter_status {
    ADAPTER_STATUS_OK = 0,       /**< Operation completed; every output is valid. */
    ADAPTER_STATUS_INVALID_ARG,  /**< A required pointer, range, or profile value is invalid; outputs are unchanged. */
    ADAPTER_STATUS_NO_MEMORY,    /**< Allocation failed atomically; caller keeps ownership of prior state. */
    ADAPTER_STATUS_INTERNAL      /**< An invariant was violated; no partial successful result is exposed. */
} adapter_status_t;
```

### QG-HDR-004 — Function Doxygen contract

Каждая public function и every public callback typedef **ДОЛЖНЫ** иметь Doxygen-блок непосредственно перед declaration. Минимальный состав:

| Doxygen element | Обязательное содержание |
|---|---|
| `@brief` | Однострочный смысл операции |
| `@details` | Алгоритм или orchestration; важные branches и limits |
| `@param` | `[in]`, `[out]` или `[in,out]`; `NULL`, size, ownership, aliasing и units |
| `@return` | Конкретный named `*_status_t`; mapping success/failure to outputs |
| `@pre` | Необходимые caller preconditions |
| `@post` | Observable state/output guarantees after success |
| `@warning` | Overflow, lifetime, concurrency или ABI hazard, если применимо |
| Thread-safety | Явное statement: safe, conditionally safe или not safe |
| Complexity | Time/space complexity, если влияет на выбор API |

Нельзя использовать `@return 0 on success` для library function. Документация обязана называть конкретный status enumerator. Единственное исключение — ISO C `int main(void)`, для которого документируется process exit code, а не library status.

### QG-HDR-005 — Ownership и allocation

Для каждого pointer parameter и возвращаемого через output pointer ресурса документация **ДОЛЖНА** явно фиксировать одну из моделей: caller-owned borrowed input, ownership transfer, caller-allocated output, library-allocated output, static storage или forbidden `NULL`. Для library-allocated memory указывается точная функция освобождения, например `json_memory_free()`.

## 4. Source и inline-code documentation gates

### QG-SRC-001 — Source file Doxygen

Каждый `.c` **ДОЛЖЕН** иметь file Doxygen, описывающий реализуемую часть public contract, алгоритмическую стратегию и критические ограничения. Если implementation split между `.c` и `.S`, оба файла должны описывать свою сторону границы.

### QG-SRC-002 — Internal и static functions

Все functions, включая `static` helpers, **ДОЛЖНЫ** иметь Doxygen-блок. Для helper достаточно внутреннего API contract, но он всё равно обязан включать `@brief`, `@details`, параметрический смысл и status/output semantics. Внутренность объекта не является основанием скрывать алгоритм.

### QG-SRC-003 — Сложные блоки должны объяснять rationale

Inline-comment обязателен перед или внутри блока, если читателю нельзя вывести причину по именам и простому control flow. В обязательную категорию входят:

- validation, normalization и bounds checking;
- arithmetic, где возможны overflow/underflow, carry или capacity boundary;
- parser state machine, token navigation и rollback/atomic writer logic;
- memory allocation, cleanup path и ownership transfer;
- synchronization, worker lifecycle, barrier и publication of results;
- branch, выбранный ради benchmark validity или prevention of compiler elimination;
- C/ASM call boundary, register preservation и representation conversion.

Комментарий объясняет **почему** и какой invariant защищается, а не дословно повторяет оператор.

```c
/* Publish the checksum only after all workers joined: observing it earlier
 * would make the benchmark result depend on scheduling rather than workload. */
context->checksum = combined_checksum;
```

### QG-SRC-004 — Алгоритм и сложность

Алгоритм, который определяет correctness или observed performance, **ДОЛЖЕН** быть описан так, чтобы reviewer мог восстановить его этапы без исполнения кода. Для recursive-descent parser, token model, benchmark dataset generator, baseline comparison, atomic writer, multi-thread coordinator и assembly kernel description обязателен отдельный `@details`/section с последовательностью этапов и complexity.

### QG-SRC-005 — C/ASM boundary

Для every assembly symbol и C wrapper вокруг него документируются calling convention, argument layout, caller/callee-saved registers, stack alignment, return/status mechanism, memory alignment, clobbers и condition flags, если они значимы. Boundary documentation должна определять, как C type representation соответствует register/memory layout.

## 5. Tests, examples и benchmark documentation gates

### QG-TEST-001 — Test file и test case intent

Каждый test source file **ДОЛЖЕН** иметь file Doxygen, объясняющий unit/module under test и oracle strategy. Каждый nontrivial test case **ДОЛЖЕН** иметь Doxygen или equivalent structured comment, описывающий setup, exact scenario, expected status/output и проверяемый invariant.

Особенно обязательно документировать `NULL` paths, invalid ranges, malformed input, ownership failure, concurrency, regression reproduction и negative tests. Комментарий должен позволять понять, почему expected failure — корректный результат, а не скрытая ошибка.

### QG-TEST-002 — Determinism и fuzzing

Документация deterministic test обязана указать fixed inputs и expected result. Документация fuzz/randomized test обязана указать seed policy, domain generation, number of cases, reference/oracle и behavior при finding failure. «Random test passed» без описанного oracle не является evidence.

### QG-EXAMPLE-001 — Examples являются исполнимым контрактом

Каждый usage example **ДОЛЖЕН** содержать build command, required includes/link flags, complete ownership cleanup и проверку named status. Пример не должен использовать non-public headers, private structs или hidden environment state. До merge он запускается как часть review/QG либо проверяется отдельной reproducer target.

### QG-BENCH-001 — Benchmark protocol и profile meaning

Benchmark wrappers документируют CLI/environment contract, profiles, warm-up, iteration semantics, ST/MT distinction, clock/measurement scope и required machine-readable protocol. Для поддержания compatibility document указывает, что строка `benchmark=...` предшествует обязательному `Benchmark finished.` marker, если данный project использует этот contract.

Любая benchmark number в документе должна сопровождаться context: revision, build configuration, platform/CPU constraints, profile, repetitions и statistical interpretation. Нельзя представлять single smoke number как stable performance conclusion.

## 6. JSON manifests и companion how-to gates

### QG-JSON-001 — Каждый JSON имеет companion document

Каждый committed `*.json` configuration/manifest **ДОЛЖЕН** иметь отдельный adjacent file `<name>.json.md`. README ссылки не заменяют данный файл. Generated result JSON может быть исключён только если он игнорируется Git и его production contract полностью описан в producer/consumer documentation.

### QG-JSON-002 — Минимальная структура `*.json.md`

| Раздел companion document | Обязательное содержание |
|---|---|
| Purpose | Что описывает JSON и какой tool его consumes |
| Location and lifecycle | Source/generated status, edit ownership, versioning |
| Schema | `schema_version`, root fields, nested fields, types, requiredness и defaults |
| Vocabulary | Allowed values и их domain semantics |
| Profile table | Identifier, scenario, input, operation, measure mode, size/capacity |
| Complete example | Валидный minimal JSON fragment без ellipses |
| How-to run | Exact C11 tool/Make command, inputs и expected output paths |
| How-to modify | Последовательность добавления profile и required validation |
| Baseline/comparison | Совместимость profile sets, regression behaviour, missing-profile policy |
| Failure handling | Malformed JSON, unsupported schema и invalid vocabulary outcomes |

### QG-JSON-003 — Schema version и compatibility

Документируются exact supported `schema_version`, backward/forward compatibility, semantic meaning of added/removed field, и status/exit behavior for unsupported schema. Любой tool, который принимает profile/results JSON, обязан иметь documented invalid-input path.

### QG-JSON-004 — JSON examples валидны

Каждый JSON fragment, declared as complete, должен быть syntactically valid. Companion how-to command должен успешно parse соответствующий JSON current version tool. Если example намеренно invalid, это должно быть явно обозначено и сопровождаться expected failure/status.

## 7. README, Usage Guide и Doxygen publishing

### QG-GUIDE-001 — README minimum

README каждого library/project включает цель, supported platform/toolchain, dependencies, clone/submodule procedure, strict build/test commands, minimal API usage, distribution content, license и contribution/quality-gate entry point. Если repository содержит submodule, указывается `git clone --recurse-submodules` и recovery command `git submodule update --init --recursive`.

### QG-GUIDE-002 — Usage Guide

Usage Guide обязателен для public library или reusable framework. Он отделяет quick start от full integration, показывает include/linking instructions, ownership/status handling, at least one failure path и complete cleanup. Каждое code example имеет surrounding prose explaining its preconditions and output.

### QG-GUIDE-003 — Generated API documentation

Если repository включает Doxygen configuration, Doxygen build должен быть reproducible in clean environment. Warnings в public API documentation являются blocking defect. Generated website/artifact не должен быть единственным местом, где определён source contract: canonical comments остаются рядом с кодом.

## 8. Formal review checklist

Reviewer заполняет checklist по **каждому изменённому artifact**, а не одним общим «documentation passed» на repository.

| Gate | Artifact-level acceptance question | Blocking when failed |
|---|---|---|
| DOC-1 | Есть ли file-level Doxygen, соответствующий роли файла? | Да |
| DOC-2 | Все types/functions/macros/enum values documented where applicable? | Да |
| DOC-3 | У каждого complex field есть inline description? | Да |
| DOC-4 | У каждого `*_status_t` code описаны cause и output state? | Да |
| DOC-5 | Doxygen function contract содержит algorithm, params, status, ownership, pre/postconditions? | Да |
| DOC-6 | Сложные blocks поясняют invariant/rationale, а не синтаксис? | Да |
| DOC-7 | Tests document scenario and exact oracle? | Да |
| DOC-8 | Каждый committed JSON имеет adjacent `*.json.md`? | Да |
| DOC-9 | JSON how-to содержит schema, vocabulary, commands и failure semantics? | Да |
| DOC-10 | README/Usage Guide examples successful against current revision? | Да |
| DOC-11 | Doxygen builds without public warnings; links and file paths valid? | Да |
| DOC-12 | Documentation reflects latest API/CLI/profile/status changes? | Да |

### Definition of Done для documentation review

Documentation work считается завершённой только при одновременном выполнении условий:

1. Все applicable gates в таблице имеют evidence для каждого artifact.
2. Doxygen и documentation build завершились без relevant warnings/errors.
3. Команды и examples воспроизведены на current revision.
4. JSON examples успешно parsed consumer tool.
5. Documentation review не выявил stale names, stale paths, duplicated contradictory contracts или undocumented ownership.
6. Исключения зафиксированы по процессу раздела 9.

## 9. Исключения и запреты

Исключение возможно только когда правило неприменимо по природе артефакта, а не ради экономии текста. Исключение документируется в review/commit description с artifact path, ID gate, причиной, risk и planned removal condition. Исключение не может отменять file documentation public API, field comments complex types, named status description, JSON companion documentation или ownership contract.

Запрещены следующие anti-patterns:

- Doxygen block, копирующий имя функции без алгоритма и conditions;
- `TODO: document later` в public API или accepted JSON;
- description primitive `int` return как «error code» вместо named status model;
- комментарий «thread safe» без scope и synchronization boundary;
- documentation example, который опускает memory cleanup или status check;
- profile JSON без document, потому что «fields очевидны»;
- reference на local path, private artifact или untracked symlink как на dependency;
- silent contract change without changelog/README/Usage Guide update.

## 10. Рекомендуемый review workflow

| Шаг | Автор изменения | Reviewer/QG executor | Evidence |
|---|---|---|---|
| 1. Contract draft | Обновляет header/source/JSON docs в одном diff | Проверяет terminology и boundary | Diff mapping implementation ↔ docs |
| 2. Local validation | Builds Doxygen and executes documented commands | Reproduces at least public example | Build/test log |
| 3. Artifact checklist | Заполняет applicable DOC gates per file | Reviews every changed artifact | Completed checklist |
| 4. Consistency pass | Searches stale names/paths/profile IDs | Confirms no contradictory docs | Search output + review notes |
| 5. Merge decision | States evidence and exceptions | Blocks unaddressed critical gaps | QG report |

## 11. Compact author checklist

Перед отправкой C11/documentation change автор подтверждает следующее:

```text
[ ] Каждый изменённый .h/.c/.S/test имеет требуемый Doxygen block.
[ ] @brief содержит только краткое резюме; алгоритм находится в @details.
[ ] Все function parameters имеют [in]/[out]/[in,out], NULL, ownership и units semantics.
[ ] Все named status enums и values объясняют failure cause и output state.
[ ] Каждое поле complex structure имеет inline comment.
[ ] Сложные algorithm/concurrency/memory/ASM blocks имеют rationale comments.
[ ] Каждый committed JSON имеет adjacent .json.md с schema и how-to.
[ ] Все examples и JSON commands выполнены на current revision.
[ ] README/Usage Guide обновлены при изменении public behaviour.
[ ] Doxygen/documentation build не выдаёт relevant warnings.
[ ] Artifact-level QG checklist приложен к review.
```

## 12. Версионирование стандарта

Документ следует хранить как versioned repository artifact, например `docs/QUALITY_GATES_DOCUMENTATION.md`. Любое ослабление blocking requirement требует отдельного согласования и versioned change. Ужесточение правила сопровождается migration period, списком affected artifacts и validation procedure.

---

**Итоговая норма:** документация в C11/benchmark project — это часть executable engineering contract. Любой API, status code, complex field, algorithmic boundary, JSON schema или benchmark profile считается неполным, пока его смысл, ограничения и проверка не могут быть восстановлены читателем из versioned documentation.
