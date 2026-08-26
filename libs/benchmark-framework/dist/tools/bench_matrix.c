/**
 * @file bench_matrix.c
 * @brief C11 executor declared benchmark matrix и raw JSON artifact writer.
 * @details
 * Tool разбирает versioned manifest, формирует immutable argv для каждого
 * `profile × ST/MT × repeat`, запускает binary через fork/execv, ограничивает
 * ожидание, валидирует обязательный machine protocol и атомарно публикует JSON.
 * Он не использует Python, shell interpolation или внешние JSON executables.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "json_lib.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_PROFILES 128U
#define FIELD_SIZE 128U
#define CAPTURE_SIZE (1024U * 1024U)

/**
 * @brief Описывает результат внутренней операции matrix runner и его ISO C exit mapping.
 * @details Все fallible tool helpers возвращают этот enum. Только main отображает
 * result в `int` как единственную границу с ISO C hosted environment.
 */
typedef enum {
    BENCH_MATRIX_STATUS_SUCCESS = 0, /**< Операция успешно завершена. */
    BENCH_MATRIX_STATUS_HELP = 1, /**< Запрошена справка; запуск matrix не требуется. */
    BENCH_MATRIX_STATUS_ARGUMENT_ERROR = 2, /**< CLI, profile manifest или output argument некорректен. */
    BENCH_MATRIX_STATUS_ALLOCATION_ERROR = 3, /**< Bounded heap allocation не удалась. */
    BENCH_MATRIX_STATUS_PROCESS_ERROR = 4, /**< fork, exec, protocol или child lifecycle завершился ошибкой. */
    BENCH_MATRIX_STATUS_IO_ERROR = 5 /**< JSON artifact I/O или system metadata операция не удалась. */
} bench_matrix_status_t;

/** @brief Представляет именованный boolean matrix/protocol predicate. */
typedef enum {
    BENCH_MATRIX_BOOLEAN_FALSE = 0, /**< Предикат не выполнен. */
    BENCH_MATRIX_BOOLEAN_TRUE = 1 /**< Предикат выполнен. */
} bench_matrix_boolean_t;

/**
 * @brief Хранит один валидированный manifest workload profile.
 * @details
 * Каждое поле копируется из JSON и проходит token validation до построения argv.
 * Фиксированные arrays исключают lifetime зависимость от parsed document и
 * ограничивают размер process command для безопасного fork/exec lifecycle.
 */
typedef struct {
    char id[FIELD_SIZE]; /**< Unique stable profile identifier for artifact grouping. */
    char input_kind[FIELD_SIZE]; /**< Adapter input pattern forwarded as `--input-kind`. */
    char operation_kind[FIELD_SIZE]; /**< Adapter operation selector forwarded as `--operation-kind`. */
    char measure_mode[FIELD_SIZE]; /**< Measurement boundary forwarded as `--measure-mode`. */
    char size_profile[FIELD_SIZE]; /**< Operand length scenario forwarded as `--size-profile`. */
    char capacity_profile[FIELD_SIZE]; /**< Storage-capacity scenario forwarded as `--capacity-profile`. */
} profile_t;

/**
 * @brief Хранит CLI configuration matrix execution.
 * @details
 * Структура объединяет paths, repetitions и воспроизводимые workload параметры.
 * parse_options отвергает нулевые values и MT total iterations, не кратные threads,
 * до allocation, запуска child или создания output artifact.
 */
typedef struct {
    const char *manifest; /**< Input versioned JSON workload profile manifest. */
    const char *output; /**< Destination path for atomically published raw JSON artifact. */
    const char *st_binary; /**< Executable implementing the ST benchmark-core protocol. */
    const char *mt_binary; /**< Executable implementing the MT benchmark-core protocol. */
    uint64_t repetitions; /**< Independent executions for every profile and mode. */
    uint64_t iterations; /**< Per-run ST iterations forwarded to the child. */
    uint64_t mt_total_iterations; /**< Total MT iterations, exactly divisible by threads. */
    uint64_t warmup; /**< Untimed warm-up operations forwarded to every benchmark. */
    uint64_t data_count; /**< Pregenerated dataset cardinality forwarded to every benchmark. */
    uint64_t seed; /**< Deterministic data-generation seed forwarded to every benchmark. */
    size_t threads; /**< MT worker count forwarded to the MT binary. */
    double timeout_seconds; /**< Parent-side wall-clock timeout for one child process. */
} options_t;

/**
 * @brief Хранит результат одного дочернего benchmark process.
 * @details
 * returncode отделён от protocol_ok: binary может завершиться с 0, но нарушить
 * contract output. output владеет captured bounded text и освобождается caller.
 */
typedef struct {
    int returncode; /**< Raw waited child exit code or signal-derived failure indication. */
    bench_matrix_boolean_t protocol_ok; /**< Whether required benchmark record and completion marker were captured. */
    char error[160]; /**< Bounded diagnostic explaining a process or protocol failure. */
    char benchmark[FIELD_SIZE]; /**< Parsed benchmark name from the machine-readable record. */
    double elapsed_seconds; /**< Parsed elapsed wall time in seconds. */
    double ns_per_call; /**< Parsed normalized duration in nanoseconds per operation. */
    char *output; /**< Owned bounded stdout/stderr capture; caller releases it with free. */
} result_t;

/** @brief Печатает детерминированную CLI usage строку.
 * @details Алгоритм не изменяет state и направляет синтаксис в stderr, поэтому
 * callers могут безопасно вызвать его для --help и invalid configuration.
 */
static void usage(const char *name)
{
    fprintf(stderr, "usage: %s --manifest FILE --output FILE --st-binary FILE --mt-binary FILE [--repetitions N] [--iterations N] [--mt-total-iterations N] [--threads N] [--warmup N] [--data-count N] [--seed N] [--timeout-seconds N]\n", name);
}

/** @brief Разбирает unsigned integer CLI value.
 * @details Алгоритм использует strtoull base 0, проверяет errno и полное
 * consumption, сохраняя support decimal и hexadecimal reproducibility seeds.
 */
static bench_matrix_status_t number_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    if (text == NULL || value == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    *value = (uint64_t)parsed;
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Разбирает строго положительный size_t CLI value.
 * @details Алгоритм делегирует uint64 parsing, затем проверяет zero и overflow
 * относительно SIZE_MAX до преобразования к platform-sized type.
 */
static bench_matrix_status_t number_size(const char *text, size_t *value)
{
    uint64_t parsed;
    if (value == NULL || number_u64(text, &parsed) != BENCH_MATRIX_STATUS_SUCCESS ||
        parsed == 0U || parsed > SIZE_MAX) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    *value = (size_t)parsed;
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Разбирает положительный finite timeout CLI value.
 * @details Алгоритм использует strtod и принимает только полностью consumed
 * positive text, исключая infinite, zero и malformed timeout configuration.
 */
static bench_matrix_status_t number_double(const char *text, double *value)
{
    char *end = NULL;
    if (text == NULL || value == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    errno = 0;
    *value = strtod(text, &end);
    return errno == 0 && end != text && *end == '\0' && *value > 0.0
        ? BENCH_MATRIX_STATUS_SUCCESS : BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
}

/** @brief Разбирает и валидирует matrix executor CLI.
 * @details Алгоритм устанавливает reproducible defaults, обрабатывает option/value
 * pairs, проверяет обязательные binary/manifest paths и требует кратность MT
 * total iterations числу threads до запуска любого child process.
 */
static bench_matrix_status_t parse_options(int argc, char **argv, options_t *options)
{
    *options = (options_t){ .repetitions = 7U, .iterations = 200000000U,
        .mt_total_iterations = 320000000U, .threads = 2U, .warmup = 10000U,
        .data_count = 4096U, .seed = UINT64_C(0x9E3779B97F4A7C15), .timeout_seconds = 1800.0 };
    for (int index = 1; index < argc; ++index) {
        const char *key = argv[index];
        const char *value;
        if (strcmp(key, "--help") == 0) return BENCH_MATRIX_STATUS_HELP;
        if (++index >= argc) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
        value = argv[index];
        if (strcmp(key, "--manifest") == 0) options->manifest = value;
        else if (strcmp(key, "--output") == 0) options->output = value;
        else if (strcmp(key, "--st-binary") == 0) options->st_binary = value;
        else if (strcmp(key, "--mt-binary") == 0) options->mt_binary = value;
        else if (strcmp(key, "--repetitions") == 0) { if (number_u64(value, &options->repetitions) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--iterations") == 0) { if (number_u64(value, &options->iterations) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--mt-total-iterations") == 0) { if (number_u64(value, &options->mt_total_iterations) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--threads") == 0) { if (number_size(value, &options->threads) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--warmup") == 0) { if (number_u64(value, &options->warmup) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--data-count") == 0) { if (number_u64(value, &options->data_count) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--seed") == 0) { if (number_u64(value, &options->seed) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--timeout-seconds") == 0) { if (number_double(value, &options->timeout_seconds) != BENCH_MATRIX_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR; }
        else return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    }
    return options->manifest != NULL && options->output != NULL && options->st_binary != NULL && options->mt_binary != NULL &&
        options->repetitions > 0U && options->iterations > 0U && options->mt_total_iterations > 0U &&
        options->data_count > 0U && options->threads > 0U && options->mt_total_iterations % options->threads == 0U
        ? BENCH_MATRIX_STATUS_SUCCESS : BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
}

/** @brief Копирует и валидирует одно строковое поле manifest profile.
 * @details Алгоритм получает JSON string token, создаёт temporary copy, отвергает
 * whitespace/quotes/equal sign и переполнение fixed destination, защищая exec argv.
 */
static bench_matrix_status_t copy_value(const json_document_t *document, size_t object,
    const char *key, char output[FIELD_SIZE])
{
    size_t token;
    char *text = NULL;
    json_boolean_t is_string;
    json_status_t json_status;
    if (document == NULL || key == NULL || output == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    json_status = json_object_get(document, object, key, &token);
    if (json_status != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    json_status = json_token_has_type(document, token, JSON_TOKEN_STRING, &is_string);
    if (json_status != JSON_STATUS_SUCCESS || is_string != JSON_BOOLEAN_TRUE) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    json_status = json_token_copy_text(document, token, &text);
    if (json_status != JSON_STATUS_SUCCESS) return json_status == JSON_STATUS_ALLOCATION_ERROR
        ? BENCH_MATRIX_STATUS_ALLOCATION_ERROR : BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    if (text[0] == '\0' || strpbrk(text, " \t\r\n=\"") != NULL || strlen(text) >= FIELD_SIZE) {
        (void)json_memory_free(text);
        return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    }
    strcpy(output, text);
    (void)json_memory_free(text);
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Загружает и проверяет versioned JSON manifest profiles.
 * @details Алгоритм требует schema_version 1 и непустой bounded profiles array,
 * копирует все workload dimensions и выполняет O(n²) duplicate-id check для
 * однозначного grouping matrix/statistics artifacts.
 */
static bench_matrix_status_t load_profiles(const char *path,
    profile_t profiles[MAX_PROFILES], size_t *count)
{
    json_document_t document;
    char error[256] = {0};
    size_t array;
    size_t schema_version;
    size_t size = 0U;
    json_boolean_t schema_is_one = JSON_BOOLEAN_FALSE;
    json_status_t json_status;
    if (path == NULL || profiles == NULL || count == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    *count = 0U;
    json_status = json_document_init(&document);
    if (json_status != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    json_status = json_document_load_file(path, &document, error, sizeof(error));
    if (json_status != JSON_STATUS_SUCCESS) {
        fprintf(stderr, "bench_matrix: %s\n", error);
        (void)json_document_destroy(&document);
        return json_status == JSON_STATUS_IO_ERROR ? BENCH_MATRIX_STATUS_IO_ERROR : BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    }
    json_status = json_object_get(&document, 0U, "schema_version", &schema_version);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_token_equals(&document, schema_version, "1", &schema_is_one);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_object_get(&document, 0U, "profiles", &array);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_array_size(&document, array, &size);
    if (json_status != JSON_STATUS_SUCCESS || schema_is_one != JSON_BOOLEAN_TRUE || size == 0U || size > MAX_PROFILES) {
        fprintf(stderr, "bench_matrix: invalid manifest schema\n");
        (void)json_document_destroy(&document);
        return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    }
    for (size_t index = 0U; index < size; ++index) {
        size_t object;
        json_boolean_t is_object = JSON_BOOLEAN_FALSE;
        json_status = json_array_get(&document, array, index, &object);
        if (json_status == JSON_STATUS_SUCCESS) json_status = json_token_has_type(&document, object, JSON_TOKEN_OBJECT, &is_object);
        if (json_status != JSON_STATUS_SUCCESS || is_object != JSON_BOOLEAN_TRUE ||
            copy_value(&document, object, "id", profiles[index].id) != BENCH_MATRIX_STATUS_SUCCESS ||
            copy_value(&document, object, "input_kind", profiles[index].input_kind) != BENCH_MATRIX_STATUS_SUCCESS ||
            copy_value(&document, object, "operation_kind", profiles[index].operation_kind) != BENCH_MATRIX_STATUS_SUCCESS ||
            copy_value(&document, object, "measure_mode", profiles[index].measure_mode) != BENCH_MATRIX_STATUS_SUCCESS ||
            copy_value(&document, object, "size_profile", profiles[index].size_profile) != BENCH_MATRIX_STATUS_SUCCESS ||
            copy_value(&document, object, "capacity_profile", profiles[index].capacity_profile) != BENCH_MATRIX_STATUS_SUCCESS) {
            fprintf(stderr, "bench_matrix: invalid profile %zu\n", index);
            (void)json_document_destroy(&document);
            return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
        }
        for (size_t prior = 0U; prior < index; ++prior) if (strcmp(profiles[prior].id, profiles[index].id) == 0) {
            fprintf(stderr, "bench_matrix: duplicate profile %s\n", profiles[index].id);
            (void)json_document_destroy(&document);
            return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
        }
    }
    *count = size;
    (void)json_document_destroy(&document);
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Запускает benchmark binary, ограничивает ожидание и захватывает output.
 * @details Алгоритм создаёт pipe, fork-ит child, перенаправляет stdout/stderr в
 * pipe и вызывает execv с уже валидированным argv. Parent polls waitpid до timeout,
 * убивает зависший child и сохраняет не более CAPTURE_SIZE bytes для JSON diagnostics.
 */
static bench_matrix_status_t capture_child(char *const command[], double timeout, result_t *result)
{
    int pipes[2];
    pid_t child;
    int status = 0;
    size_t used = 0U;
    if (command == NULL || command[0] == NULL || result == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    result->output = calloc(CAPTURE_SIZE + 1U, 1U);
    if (result->output == NULL) return BENCH_MATRIX_STATUS_ALLOCATION_ERROR;
    if (pipe(pipes) != 0) { free(result->output); result->output = NULL; return BENCH_MATRIX_STATUS_PROCESS_ERROR; }
    /* fork creates an isolated child; no shell is involved in benchmark execution. */
    child = fork();
    if (child == 0) {
        (void)close(pipes[0]);
        (void)dup2(pipes[1], STDOUT_FILENO);
        (void)dup2(pipes[1], STDERR_FILENO);
        (void)close(pipes[1]);
        execv(command[0], command);
        _exit(127);
    }
    if (child < 0) { (void)close(pipes[0]); (void)close(pipes[1]); free(result->output); result->output = NULL; return BENCH_MATRIX_STATUS_PROCESS_ERROR; }
    (void)close(pipes[1]);
    for (double elapsed = 0.0;; elapsed += 0.01) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        /* A timed-out child cannot hold CI indefinitely; reap it before returning diagnostics. */
        if (waited < 0 || elapsed >= timeout) { (void)kill(child, SIGKILL); (void)waitpid(child, &status, 0); status = -1; break; }
        { const struct timespec pause = { .tv_sec = 0, .tv_nsec = 10000000L }; (void)nanosleep(&pause, NULL); }
    }
    while (used < CAPTURE_SIZE) {
        ssize_t received = read(pipes[0], result->output + used, CAPTURE_SIZE - used);
        if (received <= 0) break;
        used += (size_t)received;
    }
    (void)close(pipes[0]);
    result->output[used] = '\0';
    result->returncode = status == -1 ? 124 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128);
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Извлекает один numeric key=value из machine-readable protocol line.
 * @details Алгоритм ищет exact key prefix, применяет strtod и принимает значение
 * только если оно завершается whitespace/end, исключая частичные совпадения ключей.
 */
static bench_matrix_status_t protocol_number(const char *line, const char *key, double *value)
{
    char pattern[64];
    const char *start;
    char *end = NULL;
    if (line == NULL || key == NULL || value == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    (void)snprintf(pattern, sizeof(pattern), "%s=", key);
    start = strstr(line, pattern);
    if (start == NULL) return BENCH_MATRIX_STATUS_PROCESS_ERROR;
    start += strlen(pattern);
    *value = strtod(start, &end);
    return end != start && (*end == ' ' || *end == '\0' || *end == '\n')
        ? BENCH_MATRIX_STATUS_SUCCESS : BENCH_MATRIX_STATUS_PROCESS_ERROR;
}

/** @brief Проверяет обязательный порядок benchmark completion protocol.
 * @details Алгоритм требует единственную benchmark= line, единственную следующую
 * Benchmark finished. line, извлекает stable benchmark name и оба timing fields.
 * Protocol violation отделяется от non-zero process status в result_t.
 */
static bench_matrix_status_t validate_protocol(result_t *result)
{
    const char *line;
    if (result == NULL || result->output == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    line = strstr(result->output, "benchmark=");
    const char *finish = strstr(result->output, "Benchmark finished.");
    const char *second = line == NULL ? NULL : strstr(line + 10, "benchmark=");
    char name[FIELD_SIZE];
    size_t length;
    /* Exactly one ordered marker pair keeps old Makefile checks and JSON parsing unambiguous. */
    if (line == NULL || second != NULL || finish == NULL || finish < line || strstr(finish + 1, "Benchmark finished.") != NULL) { strcpy(result->error, "invalid benchmark completion protocol"); return BENCH_MATRIX_STATUS_PROCESS_ERROR; }
    length = strcspn(line + 10, " \r\n");
    if (length == 0U || length >= sizeof(name)) { strcpy(result->error, "missing benchmark name"); return BENCH_MATRIX_STATUS_PROCESS_ERROR; }
    memcpy(name, line + 10, length); name[length] = '\0';
    if (protocol_number(line, "elapsed_seconds", &result->elapsed_seconds) != BENCH_MATRIX_STATUS_SUCCESS || protocol_number(line, "ns_per_call", &result->ns_per_call) != BENCH_MATRIX_STATUS_SUCCESS) { strcpy(result->error, "missing timing protocol fields"); return BENCH_MATRIX_STATUS_PROCESS_ERROR; }
    strcpy(result->benchmark, name);
    result->protocol_ok = BENCH_MATRIX_BOOLEAN_TRUE;
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Сериализует validated workload profile в JSON object.
 * @details Алгоритм выводит все шесть dimensions в стабильном порядке и делегирует
 * string escaping json-lib, чтобы manifests/artifacts сохраняли machine readability.
 */
static bench_matrix_status_t json_profile(json_writer_t *writer, const profile_t *profile)
{
    if (writer == NULL || writer->stream == NULL || profile == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    if (json_writer_write_raw(writer, "{\"id\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->id) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"input_kind\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->input_kind) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"operation_kind\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->operation_kind) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"measure_mode\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->measure_mode) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"size_profile\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->size_profile) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"capacity_profile\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->capacity_profile) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, "}") != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
    return BENCH_MATRIX_STATUS_SUCCESS;
}

/** @brief Сериализует один process result как matrix sample JSON object.
 * @details Алгоритм всегда сохраняет profile/mode/repeat/returncode/stdout, затем
 * добавляет parsed protocol только для valid completion либо protocol_error иначе.
 * Это сохраняет diagnosability неуспешных child runs.
 */
static bench_matrix_status_t json_sample(json_writer_t *writer, const result_t *result,
    const profile_t *profile, const char *mode, uint64_t repeat)
{
    char numeric[192];
    int written;
    if (writer == NULL || writer->stream == NULL || result == NULL || profile == NULL || mode == NULL) return BENCH_MATRIX_STATUS_ARGUMENT_ERROR;
    written = snprintf(numeric, sizeof(numeric), ",\"repeat_index\":%" PRIu64 ",\"returncode\":%d,\"stdout\":", repeat, result->returncode);
    if (written < 0 || (size_t)written >= sizeof(numeric)) return BENCH_MATRIX_STATUS_IO_ERROR;
    if (json_writer_write_raw(writer, "{\"profile_id\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, profile->id) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"mode\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, mode) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, numeric) != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, result->output == NULL ? "" : result->output) != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
    if (result->protocol_ok == BENCH_MATRIX_BOOLEAN_TRUE) {
        written = snprintf(numeric, sizeof(numeric), ",\"elapsed_seconds\":%.9f,\"ns_per_call\":%.9f}", result->elapsed_seconds, result->ns_per_call);
        if (written < 0 || (size_t)written >= sizeof(numeric) ||
            json_writer_write_raw(writer, ",\"protocol\":{\"benchmark\":") != JSON_STATUS_SUCCESS ||
            json_writer_write_string(writer, result->benchmark) != JSON_STATUS_SUCCESS ||
            json_writer_write_raw(writer, numeric) != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
    } else if (json_writer_write_raw(writer, ",\"protocol_error\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, result->error) != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
    return json_writer_write_raw(writer, "}") == JSON_STATUS_SUCCESS
        ? BENCH_MATRIX_STATUS_SUCCESS : BENCH_MATRIX_STATUS_IO_ERROR;
}

/** @brief Сериализует runtime host metadata в JSON object.
 * @details Алгоритм получает uname, logical CPU count и current scheduler affinity;
 * отсутствующая affinity не является ошибкой и представляется пустым array.
 */
static bench_matrix_status_t json_host(json_writer_t *writer)
{
    struct utsname name;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t set;
    char numeric[64];
    int written;
    if (writer == NULL || writer->stream == NULL || uname(&name) != 0) return BENCH_MATRIX_STATUS_IO_ERROR;
    written = snprintf(numeric, sizeof(numeric), ",\"logical_cpu_count\":%ld,\"cpu_affinity\":[", cpus);
    if (written < 0 || (size_t)written >= sizeof(numeric) ||
        json_writer_write_raw(writer, "{\"tool\":\"bench_matrix-c11\",\"system\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, name.sysname) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"release\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, name.release) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, ",\"machine\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(writer, name.machine) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(writer, numeric) != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        int first = 1;
        for (int index = 0; index < CPU_SETSIZE; ++index) if (CPU_ISSET(index, &set)) {
            written = snprintf(numeric, sizeof(numeric), "%s%d", first ? "" : ",", index);
            if (written < 0 || (size_t)written >= sizeof(numeric) ||
                json_writer_write_raw(writer, numeric) != JSON_STATUS_SUCCESS) return BENCH_MATRIX_STATUS_IO_ERROR;
            first = 0;
        }
    }
    return json_writer_write_raw(writer, "]}") == JSON_STATUS_SUCCESS
        ? BENCH_MATRIX_STATUS_SUCCESS : BENCH_MATRIX_STATUS_IO_ERROR;
}

/**
 * @brief Выполняет полный declared benchmark matrix.
 * @param argc Число CLI arguments.
 * @param argv CLI argument vector.
 * @return 0 при успешных samples; 1 при process/protocol failure; 2 при config/I/O error.
 * @details Алгоритм валидирует CLI и manifest, открывает atomic artifact, записывает
 * metadata/profiles, последовательно запускает profile × mode × repeat, serializes
 * sample results и публикует JSON только после complete execution.
 */
int main(int argc, char **argv)
{
    options_t options;
    profile_t profiles[MAX_PROFILES];
    size_t profile_count;
    char error[256] = {0};
    char fragment[256];
    json_writer_t writer = {0};
    json_status_t json_status;
    uint64_t sample_count = 0U;
    int failures = 0;
    bench_matrix_status_t option_result = parse_options(argc, argv, &options);
    if (option_result == BENCH_MATRIX_STATUS_HELP) { usage(argv[0]); return 0; }
    if (option_result != BENCH_MATRIX_STATUS_SUCCESS ||
        load_profiles(options.manifest, profiles, &profile_count) != BENCH_MATRIX_STATUS_SUCCESS ||
        access(options.st_binary, X_OK) != 0 || access(options.mt_binary, X_OK) != 0) {
        usage(argv[0]);
        return 2;
    }
    json_status = json_writer_open(options.output, &writer, error, sizeof(error));
    if (json_status != JSON_STATUS_SUCCESS) { fprintf(stderr, "bench_matrix: %s\n", error); return 2; }
    if (json_writer_write_raw(&writer, "{\"schema_version\":1,\"host\":") != JSON_STATUS_SUCCESS ||
        json_host(&writer) != BENCH_MATRIX_STATUS_SUCCESS ||
        snprintf(fragment, sizeof(fragment), ",\"configuration\":{\"repetitions\":%" PRIu64 ",\"iterations\":%" PRIu64 ",\"mt_total_iterations\":%" PRIu64 ",\"threads\":%zu,\"warmup\":%" PRIu64 ",\"data_count\":%" PRIu64 ",\"seed\":%" PRIu64 "},\"profiles\":[", options.repetitions, options.iterations, options.mt_total_iterations, options.threads, options.warmup, options.data_count, options.seed) < 0 ||
        json_writer_write_raw(&writer, fragment) != JSON_STATUS_SUCCESS) {
        (void)json_writer_abort(&writer);
        return 2;
    }
    for (size_t index = 0U; index < profile_count; ++index) {
        if ((index != 0U && json_writer_write_raw(&writer, ",") != JSON_STATUS_SUCCESS) ||
            json_profile(&writer, &profiles[index]) != BENCH_MATRIX_STATUS_SUCCESS) {
            (void)json_writer_abort(&writer);
            return 2;
        }
    }
    if (json_writer_write_raw(&writer, "],\"samples\":[") != JSON_STATUS_SUCCESS) {
        (void)json_writer_abort(&writer);
        return 2;
    }
    for (size_t profile_index = 0U; profile_index < profile_count; ++profile_index) for (int mode_index = 0; mode_index < 2; ++mode_index) for (uint64_t repeat = 0U; repeat < options.repetitions; ++repeat) {
        char iterations[32], total[32], threads[32], warmup[32], data_count[32], seed[32];
        const char *mode = mode_index == 0 ? "st" : "mt";
        const char *binary = mode_index == 0 ? options.st_binary : options.mt_binary;
        result_t result = {0};
        (void)snprintf(iterations, sizeof(iterations), "%" PRIu64, options.iterations);
        (void)snprintf(total, sizeof(total), "%" PRIu64, options.mt_total_iterations);
        (void)snprintf(threads, sizeof(threads), "%zu", options.threads);
        (void)snprintf(warmup, sizeof(warmup), "%" PRIu64, options.warmup);
        (void)snprintf(data_count, sizeof(data_count), "%" PRIu64, options.data_count);
        (void)snprintf(seed, sizeof(seed), "%" PRIu64, options.seed);
        char *command_st[] = { (char *)binary, "--iterations", iterations, "--warmup", warmup, "--data-count", data_count, "--seed", seed, "--input-kind", profiles[profile_index].input_kind, "--operation-kind", profiles[profile_index].operation_kind, "--measure-mode", profiles[profile_index].measure_mode, "--size-profile", profiles[profile_index].size_profile, "--capacity-profile", profiles[profile_index].capacity_profile, NULL };
        char *command_mt[] = { (char *)binary, "--threads", threads, "--total-iterations", total, "--warmup", warmup, "--data-count", data_count, "--seed", seed, "--input-kind", profiles[profile_index].input_kind, "--operation-kind", profiles[profile_index].operation_kind, "--measure-mode", profiles[profile_index].measure_mode, "--size-profile", profiles[profile_index].size_profile, "--capacity-profile", profiles[profile_index].capacity_profile, NULL };
        char *const *command = mode_index == 0 ? command_st : command_mt;
        /* Each profile/mode/repeat receives a fresh process and a fully explicit argv. */
        if (capture_child(command, options.timeout_seconds, &result) != BENCH_MATRIX_STATUS_SUCCESS) { result.returncode = 125; strcpy(result.error, "cannot execute benchmark"); }
        if (result.returncode != 0 && result.error[0] == '\0') strcpy(result.error, "benchmark returned non-zero status");
        if (result.returncode == 0) (void)validate_protocol(&result);
        if ((sample_count != 0U && json_writer_write_raw(&writer, ",") != JSON_STATUS_SUCCESS) ||
            json_sample(&writer, &result, &profiles[profile_index], mode, repeat) != BENCH_MATRIX_STATUS_SUCCESS) {
            free(result.output);
            (void)json_writer_abort(&writer);
            return 2;
        }
        ++sample_count;
        if (result.protocol_ok != BENCH_MATRIX_BOOLEAN_TRUE) ++failures;
        else printf("%s %s repeat=%" PRIu64 "/%" PRIu64 " ns_per_call=%.3f\n", profiles[profile_index].id, mode, repeat + 1U, options.repetitions, result.ns_per_call);
        free(result.output);
    }
    if (snprintf(fragment, sizeof(fragment), "],\"failures\":%d}\n", failures) < 0 ||
        json_writer_write_raw(&writer, fragment) != JSON_STATUS_SUCCESS ||
        json_writer_commit(&writer, error, sizeof(error)) != JSON_STATUS_SUCCESS) {
        fprintf(stderr, "bench_matrix: %s\n", error);
        (void)json_writer_abort(&writer);
        return 2;
    }
    if (failures != 0) { fprintf(stderr, "bench_matrix: failures=%d; see %s\n", failures, options.output); return 1; }
    printf("bench_matrix: wrote %" PRIu64 " samples to %s\n", sample_count, options.output);
    return 0;
}
