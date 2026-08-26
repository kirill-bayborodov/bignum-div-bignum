/**
 * @file benchmark_stats.c
 * @brief C11 агрегатор matrix JSON и baseline regression gate.
 * @details
 * Tool читает raw matrix либо ранее созданный summary, группирует successful
 * samples по profile_id × mode, вычисляет устойчивые descriptive metrics и
 * сравнивает candidate с reviewed baseline. JSON output пишется atomically через
 * json-lib; отсутствие группы или подтверждённая регрессия возвращают exit code 1.
 */
#define _POSIX_C_SOURCE 200809L
#include "json_lib.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GROUPS 512U
#define NAME_SIZE 128U

/**
 * @brief Описывает итог каждой fallible операции statistics tool.
 * @details Helpers возвращают только этот enum; `main` единожды отображает его
 * в portable ISO C integer exit status для shell/Makefile boundary.
 */
typedef enum {
    BENCHMARK_STATS_STATUS_SUCCESS = 0, /**< Обработка и JSON publication успешны. */
    BENCHMARK_STATS_STATUS_HELP = 1, /**< Запрошена CLI справка. */
    BENCHMARK_STATS_STATUS_ARGUMENT_ERROR = 2, /**< CLI или входной JSON schema некорректны. */
    BENCHMARK_STATS_STATUS_ALLOCATION_ERROR = 3, /**< Memory allocation для samples/metrics не удалась. */
    BENCHMARK_STATS_STATUS_IO_ERROR = 4, /**< JSON load/write/atomic publication не удалась. */
    BENCHMARK_STATS_STATUS_REGRESSION = 5 /**< Regression gate обнаружил превышение policy threshold. */
} benchmark_stats_status_t;

/** @brief Представляет именованный boolean result statistics predicate. */
typedef enum {
    BENCHMARK_STATS_BOOLEAN_FALSE = 0, /**< Предикат не выполнен. */
    BENCHMARK_STATS_BOOLEAN_TRUE = 1 /**< Предикат выполнен. */
} benchmark_stats_boolean_t;

/** @brief Представляет индекс aggregation group либо -1 при отсутствии. */
typedef int benchmark_stats_group_index_t;

/**
 * @brief Хранит CLI configuration aggregation и optional baseline gate.
 * @details
 * threshold и mad_multiplier неотрицательны; allow_regressions оставляет summary
 * available для exploratory analysis, но не скрывает regression records в JSON.
 */
typedef struct {
    const char *input; /**< Candidate raw matrix or normalized summary JSON path. */
    const char *output; /**< Atomic destination path for normalized JSON summary. */
    const char *baseline; /**< Optional reviewed summary JSON used by regression gate. */
    double threshold; /**< Allowed relative performance regression in percent. */
    double mad_multiplier; /**< MAD-based noise allowance multiplier for the gate. */
    benchmark_stats_boolean_t allow_regressions; /**< Whether a detected regression still maps to successful tool status. */
} options_t;

/**
 * @brief Хранит samples и производные metrics одной profile_id × mode группы.
 * @details
 * values — dynamically grown caller-owned array raw timings. После sorting
 * calculate_metrics заполняет minimum, maximum, mean, median, sample stdev и MAD;
 * summary-only baseline может иметь count 0 и только заранее прочитанные metrics.
 */
typedef struct {
    char profile_id[NAME_SIZE]; /**< Stable profile dimension used for candidate/baseline matching. */
    char mode[4]; /**< Benchmark execution mode: `st` or `mt`. */
    double *values; /**< Owned dynamically grown raw ns-per-call sample array. */
    size_t count; /**< Number initialized raw samples; zero for summary-only baseline. */
    size_t capacity; /**< Allocated `values` capacity in double elements. */
    double minimum; /**< Smallest observed ns-per-call metric. */
    double maximum; /**< Largest observed ns-per-call metric. */
    double mean; /**< Arithmetic mean of raw values. */
    double median; /**< Median of sorted raw values. */
    double stdev; /**< Sample standard deviation of raw values. */
    double mad; /**< Median absolute deviation around median. */
} group_t;

/** @brief Печатает usage statistics CLI.
 * @details Алгоритм не меняет aggregation state и описывает optional baseline,
 * threshold, MAD multiplier и allow-regressions semantics в stderr.
 */
static void usage(const char *name)
{
    fprintf(stderr, "usage: %s --input FILE --output FILE [--baseline FILE] [--threshold-pct N] [--noise-mad-multiplier N] [--allow-regressions]\n", name);
}

/** @brief Разбирает неотрицательный finite numeric CLI option.
 * @details Алгоритм применяет strtod и принимает значение только при полном
 * consumption, finite result и domain value >= 0 для threshold/MAD multiplier.
 */
static benchmark_stats_status_t number_double(const char *text, double *value)
{
    char *end = NULL;
    if (text == NULL || value == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    errno = 0;
    *value = strtod(text, &end);
    return errno == 0 && end != text && *end == '\0' && isfinite(*value) && *value >= 0.0
        ? BENCHMARK_STATS_STATUS_SUCCESS : BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
}

/** @brief Разбирает aggregation CLI и его policy flags.
 * @details Алгоритм задаёт default threshold 5% и MAD multiplier 3, обрабатывает
 * optional baseline и allow-regressions, затем требует input/output paths.
 */
static benchmark_stats_status_t parse_options(int argc, char **argv, options_t *options)
{
    *options = (options_t){ .threshold = 5.0, .mad_multiplier = 3.0 };
    for (int index = 1; index < argc; ++index) {
        const char *key = argv[index];
        const char *value;
        if (strcmp(key, "--help") == 0) return BENCHMARK_STATS_STATUS_HELP;
        if (strcmp(key, "--allow-regressions") == 0) { options->allow_regressions = BENCHMARK_STATS_BOOLEAN_TRUE; continue; }
        if (++index >= argc) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
        value = argv[index];
        if (strcmp(key, "--input") == 0) options->input = value;
        else if (strcmp(key, "--output") == 0) options->output = value;
        else if (strcmp(key, "--baseline") == 0) options->baseline = value;
        else if (strcmp(key, "--threshold-pct") == 0) { if (number_double(value, &options->threshold) != BENCHMARK_STATS_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR; }
        else if (strcmp(key, "--noise-mad-multiplier") == 0) { if (number_double(value, &options->mad_multiplier) != BENCHMARK_STATS_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR; }
        else return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    return options->input != NULL && options->output != NULL
        ? BENCHMARK_STATS_STATUS_SUCCESS : BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
}

/** @brief Освобождает raw values всех metrics groups.
 * @details Алгоритм проходит ровно count groups и освобождает nullable arrays;
 * он одинаково корректен для matrix groups и summary-only groups с count zero.
 */
static void groups_destroy(group_t *groups, size_t count)
{
    for (size_t index = 0U; index < count; ++index) free(groups[index].values);
}

/** @brief Находит или создаёт profile_id × mode aggregation group.
 * @details Алгоритм линейно сравнивает stable IDs, а при отсутствии проверяет
 * MAX_GROUPS/name bounds/mode domain и инициализирует новый zeroed group.
 */
static benchmark_stats_group_index_t group_find_or_add(group_t groups[MAX_GROUPS], size_t *count, const char *id, const char *mode)
{
    for (size_t index = 0U; index < *count; ++index) if (strcmp(groups[index].profile_id, id) == 0 && strcmp(groups[index].mode, mode) == 0) return (int)index;
    if (*count == MAX_GROUPS || strlen(id) >= NAME_SIZE || (strcmp(mode, "st") != 0 && strcmp(mode, "mt") != 0)) return -1;
    groups[*count] = (group_t){0};
    strcpy(groups[*count].profile_id, id);
    strcpy(groups[*count].mode, mode);
    return (int)(*count)++;
}

/** @brief Добавляет raw timing sample в dynamically grown group array.
 * @details Алгоритм удваивает capacity начиная с восьми slots, проверяет realloc
 * и изменяет count только после успешного allocation, сохраняя failure atomicity.
 */
static benchmark_stats_status_t group_append(group_t *group, double value)
{
    if (group->count == group->capacity) {
        /* Geometric growth keeps append amortized O(1) without changing count on allocation failure. */
        const size_t capacity = group->capacity == 0U ? 8U : group->capacity * 2U;
        double *values = realloc(group->values, capacity * sizeof(*values));
        if (values == NULL) return BENCHMARK_STATS_STATUS_ALLOCATION_ERROR;
        group->values = values;
        group->capacity = capacity;
    }
    group->values[group->count++] = value;
    return BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Копирует bounded JSON string в fixed aggregation identifier buffer.
 * @details Алгоритм требует JM_STRING, получает temporary token copy, проверяет
 * NAME_SIZE и только затем передаёт ownership-safe text в destination array.
 */
static benchmark_stats_status_t copy_text(const json_document_t *document, size_t token,
    char output[NAME_SIZE])
{
    char *value = NULL;
    json_boolean_t is_string;
    json_status_t json_status;
    if (document == NULL || output == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    json_status = json_token_has_type(document, token, JSON_TOKEN_STRING, &is_string);
    if (json_status != JSON_STATUS_SUCCESS || is_string != JSON_BOOLEAN_TRUE) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    json_status = json_token_copy_text(document, token, &value);
    if (json_status != JSON_STATUS_SUCCESS) return json_status == JSON_STATUS_ALLOCATION_ERROR
        ? BENCHMARK_STATS_STATUS_ALLOCATION_ERROR : BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    if (strlen(value) >= NAME_SIZE) {
        (void)json_memory_free(value);
        return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    strcpy(output, value);
    (void)json_memory_free(value);
    return BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Извлекает successful protocol timings из raw matrix JSON.
 * @details Алгоритм проходит samples array, пропускает samples без protocol,
 * валидирует profile_id/mode/ns_per_call и добавляет значения к matching group.
 * Отсутствие хотя бы одной successful group считается invalid input.
 */
static benchmark_stats_status_t load_matrix(const json_document_t *document,
    group_t groups[MAX_GROUPS], size_t *group_count)
{
    size_t samples;
    size_t size;
    json_status_t json_status;
    if (document == NULL || groups == NULL || group_count == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    json_status = json_object_get(document, 0U, "samples", &samples);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_array_size(document, samples, &size);
    if (json_status != JSON_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    for (size_t index = 0U; index < size; ++index) {
        size_t sample;
        size_t protocol;
        size_t profile_id;
        size_t mode_token;
        size_t ns_per_call;
        char id[NAME_SIZE], mode[NAME_SIZE];
        double value;
        benchmark_stats_group_index_t group;
        json_status = json_array_get(document, samples, index, &sample);
        if (json_status != JSON_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
        json_status = json_object_get(document, sample, "protocol", &protocol);
        if (json_status == JSON_STATUS_NOT_FOUND) continue;
        if (json_status != JSON_STATUS_SUCCESS ||
            json_object_get(document, sample, "profile_id", &profile_id) != JSON_STATUS_SUCCESS ||
            json_object_get(document, sample, "mode", &mode_token) != JSON_STATUS_SUCCESS ||
            json_object_get(document, protocol, "ns_per_call", &ns_per_call) != JSON_STATUS_SUCCESS ||
            copy_text(document, profile_id, id) != BENCHMARK_STATS_STATUS_SUCCESS ||
            copy_text(document, mode_token, mode) != BENCHMARK_STATS_STATUS_SUCCESS ||
            json_token_to_double(document, ns_per_call, &value) != JSON_STATUS_SUCCESS || !isfinite(value) || value < 0.0 ||
            (group = group_find_or_add(groups, group_count, id, mode)) < 0 ||
            group_append(&groups[group], value) != BENCHMARK_STATS_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    return *group_count == 0U ? BENCHMARK_STATS_STATUS_ARGUMENT_ERROR : BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Извлекает заранее агрегированные groups из summary JSON.
 * @details Алгоритм проходит profiles array, копирует ID/mode и читает полный
 * metric set. Такой document применим как reviewed baseline без raw samples.
 */
static benchmark_stats_status_t load_summary(const json_document_t *document,
    group_t groups[MAX_GROUPS], size_t *group_count)
{
    size_t profiles;
    size_t size;
    json_status_t json_status;
    if (document == NULL || groups == NULL || group_count == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    json_status = json_object_get(document, 0U, "profiles", &profiles);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_array_size(document, profiles, &size);
    if (json_status != JSON_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    for (size_t index = 0U; index < size; ++index) {
        size_t profile, metrics, profile_id, mode_token, minimum, maximum, mean, median_token, stdev, mad;
        char id[NAME_SIZE], mode[NAME_SIZE];
        benchmark_stats_group_index_t group;
        json_status = json_array_get(document, profiles, index, &profile);
        if (json_status != JSON_STATUS_SUCCESS ||
            json_object_get(document, profile, "metrics", &metrics) != JSON_STATUS_SUCCESS ||
            json_object_get(document, profile, "profile_id", &profile_id) != JSON_STATUS_SUCCESS ||
            json_object_get(document, profile, "mode", &mode_token) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "minimum_ns_per_call", &minimum) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "maximum_ns_per_call", &maximum) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "mean_ns_per_call", &mean) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "median_ns_per_call", &median_token) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "stdev_ns_per_call", &stdev) != JSON_STATUS_SUCCESS ||
            json_object_get(document, metrics, "mad_ns_per_call", &mad) != JSON_STATUS_SUCCESS ||
            copy_text(document, profile_id, id) != BENCHMARK_STATS_STATUS_SUCCESS ||
            copy_text(document, mode_token, mode) != BENCHMARK_STATS_STATUS_SUCCESS ||
            (group = group_find_or_add(groups, group_count, id, mode)) < 0 ||
            json_token_to_double(document, minimum, &groups[group].minimum) != JSON_STATUS_SUCCESS ||
            json_token_to_double(document, maximum, &groups[group].maximum) != JSON_STATUS_SUCCESS ||
            json_token_to_double(document, mean, &groups[group].mean) != JSON_STATUS_SUCCESS ||
            json_token_to_double(document, median_token, &groups[group].median) != JSON_STATUS_SUCCESS ||
            json_token_to_double(document, stdev, &groups[group].stdev) != JSON_STATUS_SUCCESS ||
            json_token_to_double(document, mad, &groups[group].mad) != JSON_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    return *group_count == 0U ? BENCHMARK_STATS_STATUS_ARGUMENT_ERROR : BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Предоставляет total ordering double values для qsort.
 * @details Алгоритм возвращает -1/0/1 через pairwise comparison; upstream input
 * validation исключает non-finite values, поэтому ordering остаётся deterministic.
 */
static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return a < b ? -1 : a > b;
}

/** @brief Вычисляет median уже отсортированного numeric array.
 * @details Алгоритм выбирает central element для odd count либо arithmetic mean
 * двух central values для even count; caller гарантирует count > 0.
 */
static double median(const double *values, size_t count)
{
    return count % 2U == 0U ? (values[count / 2U - 1U] + values[count / 2U]) / 2.0 : values[count / 2U];
}

/** @brief Вычисляет устойчивые descriptive metrics одной raw-sample group.
 * @details Алгоритм сортирует samples, вычисляет min/max/mean, sample standard
 * deviation с divisor n-1, median и median absolute deviation от median. MAD
 * формирует noise floor, устойчивый к единичным noisy timings.
 */
static benchmark_stats_status_t calculate_metrics(group_t *group)
{
    double sum = 0.0;
    double squared = 0.0;
    double *deviations;
    if (group == NULL || group->count == 0U) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    qsort(group->values, group->count, sizeof(*group->values), compare_double);
    group->minimum = group->values[0];
    group->maximum = group->values[group->count - 1U];
    for (size_t index = 0U; index < group->count; ++index) sum += group->values[index];
    group->mean = sum / (double)group->count;
    for (size_t index = 0U; index < group->count; ++index) { const double delta = group->values[index] - group->mean; squared += delta * delta; }
    /* Use sample stdev (n - 1) and median/MAD so one noisy run does not define the gate. */
    group->stdev = group->count > 1U ? sqrt(squared / (double)(group->count - 1U)) : 0.0;
    group->median = median(group->values, group->count);
    deviations = malloc(group->count * sizeof(*deviations));
    if (deviations == NULL) return BENCHMARK_STATS_STATUS_ALLOCATION_ERROR;
    for (size_t index = 0U; index < group->count; ++index) deviations[index] = fabs(group->values[index] - group->median);
    qsort(deviations, group->count, sizeof(*deviations), compare_double);
    group->mad = median(deviations, group->count);
    free(deviations);
    return BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Загружает matrix либо summary JSON в normalized metrics groups.
 * @details Алгоритм проверяет schema_version 1, выбирает loader по наличию samples,
 * затем агрегирует raw matrix groups или принимает summary metrics as-is.
 */
static benchmark_stats_status_t load_groups(const char *path,
    group_t groups[MAX_GROUPS], size_t *count)
{
    json_document_t document;
    char error[256] = {0};
    size_t schema_version;
    size_t samples;
    json_boolean_t schema_is_one = JSON_BOOLEAN_FALSE;
    json_status_t json_status;
    benchmark_stats_status_t result;
    if (path == NULL || groups == NULL || count == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    *count = 0U;
    json_status = json_document_init(&document);
    if (json_status != JSON_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    json_status = json_document_load_file(path, &document, error, sizeof(error));
    if (json_status != JSON_STATUS_SUCCESS) {
        fprintf(stderr, "benchmark_stats: %s\n", error);
        (void)json_document_destroy(&document);
        return json_status == JSON_STATUS_IO_ERROR ? BENCHMARK_STATS_STATUS_IO_ERROR : BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    json_status = json_object_get(&document, 0U, "schema_version", &schema_version);
    if (json_status == JSON_STATUS_SUCCESS) json_status = json_token_equals(&document, schema_version, "1", &schema_is_one);
    if (json_status != JSON_STATUS_SUCCESS || schema_is_one != JSON_BOOLEAN_TRUE) {
        (void)json_document_destroy(&document);
        return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    }
    json_status = json_object_get(&document, 0U, "samples", &samples);
    result = json_status == JSON_STATUS_SUCCESS ? load_matrix(&document, groups, count)
        : json_status == JSON_STATUS_NOT_FOUND ? load_summary(&document, groups, count)
        : BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    (void)json_document_destroy(&document);
    if (result != BENCHMARK_STATS_STATUS_SUCCESS) return result;
    for (size_t index = 0U; index < *count; ++index) {
        if (groups[index].count > 0U && calculate_metrics(&groups[index]) != BENCHMARK_STATS_STATUS_SUCCESS) return BENCHMARK_STATS_STATUS_ALLOCATION_ERROR;
    }
    return BENCHMARK_STATS_STATUS_SUCCESS;
}

/** @brief Ищет exact profile_id × mode group в заданном наборе.
 * @details Алгоритм линейно сравнивает оба stable dimensions и возвращает -1 при
 * отсутствии, что используется для симметричной проверки candidate/baseline sets.
 */
static int find_group(const group_t *groups, size_t count, const group_t *needle)
{
    for (size_t index = 0U; index < count; ++index) if (strcmp(groups[index].profile_id, needle->profile_id) == 0 && strcmp(groups[index].mode, needle->mode) == 0) return (int)index;
    return -1;
}

/** @brief Сериализует полный metrics set одной group в JSON object.
 * @details Алгоритм фиксирует field names и precision для raw-derived и summary
 * groups, поэтому output снова допускается как baseline input следующего запуска.
 */
static benchmark_stats_status_t write_metrics(json_writer_t *writer, const group_t *group)
{
    char fragment[512];
    int written;
    if (writer == NULL || group == NULL) return BENCHMARK_STATS_STATUS_ARGUMENT_ERROR;
    written = snprintf(fragment, sizeof(fragment), "{\"sample_count\":%zu,\"minimum_ns_per_call\":%.9f,\"maximum_ns_per_call\":%.9f,\"mean_ns_per_call\":%.9f,\"median_ns_per_call\":%.9f,\"stdev_ns_per_call\":%.9f,\"mad_ns_per_call\":%.9f}", group->count, group->minimum, group->maximum, group->mean, group->median, group->stdev, group->mad);
    if (written < 0 || (size_t)written >= sizeof(fragment)) return BENCHMARK_STATS_STATUS_IO_ERROR;
    return json_writer_write_raw(writer, fragment) == JSON_STATUS_SUCCESS
        ? BENCHMARK_STATS_STATUS_SUCCESS : BENCHMARK_STATS_STATUS_IO_ERROR;
}

/**
 * @brief Агрегирует candidate и применяет optional baseline regression policy.
 * @param argc Число CLI arguments.
 * @param argv CLI argument vector.
 * @return 0 при clean comparison; 1 при regression/missing group; 2 при input error.
 * @details Алгоритм normalizes input documents to groups, creates atomic summary,
 * сравнивает medians с threshold и MAD noise floor, симметрично проверяет group
 * sets и публикует JSON до возврата policy exit code.
 */
int main(int argc, char **argv)
{
    options_t options;
    group_t candidate[MAX_GROUPS] = {0};
    group_t baseline[MAX_GROUPS] = {0};
    size_t candidate_count, baseline_count = 0U;
    char error[256] = {0};
    char fragment[512];
    json_writer_t writer = {0};
    json_status_t json_status;
    int regressions = 0;
    int missing = 0;
    benchmark_stats_status_t option_result = parse_options(argc, argv, &options);
    if (option_result == BENCHMARK_STATS_STATUS_HELP) { usage(argv[0]); return 0; }
    if (option_result != BENCHMARK_STATS_STATUS_SUCCESS ||
        load_groups(options.input, candidate, &candidate_count) != BENCHMARK_STATS_STATUS_SUCCESS ||
        (options.baseline != NULL && load_groups(options.baseline, baseline, &baseline_count) != BENCHMARK_STATS_STATUS_SUCCESS)) {
        usage(argv[0]);
        groups_destroy(candidate, candidate_count);
        groups_destroy(baseline, baseline_count);
        return 2;
    }
    json_status = json_writer_open(options.output, &writer, error, sizeof(error));
    if (json_status != JSON_STATUS_SUCCESS) {
        fprintf(stderr, "benchmark_stats: %s\n", error);
        groups_destroy(candidate, candidate_count);
        groups_destroy(baseline, baseline_count);
        return 2;
    }
    if (json_writer_write_raw(&writer, "{\"schema_version\":1,\"candidate\":") != JSON_STATUS_SUCCESS ||
        json_writer_write_string(&writer, options.input) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(&writer, ",\"baseline\":") != JSON_STATUS_SUCCESS ||
        (options.baseline == NULL ? json_writer_write_raw(&writer, "null") : json_writer_write_string(&writer, options.baseline)) != JSON_STATUS_SUCCESS ||
        json_writer_write_raw(&writer, ",\"profiles\":[") != JSON_STATUS_SUCCESS) goto io_error;
    for (size_t index = 0U; index < candidate_count; ++index) {
        if ((index != 0U && json_writer_write_raw(&writer, ",") != JSON_STATUS_SUCCESS) ||
            json_writer_write_raw(&writer, "{\"profile_id\":") != JSON_STATUS_SUCCESS ||
            json_writer_write_string(&writer, candidate[index].profile_id) != JSON_STATUS_SUCCESS ||
            json_writer_write_raw(&writer, ",\"mode\":") != JSON_STATUS_SUCCESS ||
            json_writer_write_string(&writer, candidate[index].mode) != JSON_STATUS_SUCCESS ||
            json_writer_write_raw(&writer, ",\"metrics\":") != JSON_STATUS_SUCCESS ||
            write_metrics(&writer, &candidate[index]) != BENCHMARK_STATS_STATUS_SUCCESS ||
            json_writer_write_raw(&writer, "}") != JSON_STATUS_SUCCESS) goto io_error;
    }
    if (json_writer_write_raw(&writer, "],\"comparisons\":[") != JSON_STATUS_SUCCESS) goto io_error;
    if (options.baseline != NULL) {
        int comparisons = 0;
        for (size_t index = 0U; index < candidate_count; ++index) {
            const benchmark_stats_group_index_t match = find_group(baseline, baseline_count, &candidate[index]);
            if (match < 0) { ++missing; continue; }
            /* Confirm slowdown against both policy threshold and baseline's observed MAD noise floor. */
            {
                const group_t *base = &baseline[match];
                const double relative = base->median == 0.0 ? (candidate[index].median == 0.0 ? 0.0 : INFINITY) : 100.0 * (candidate[index].median - base->median) / base->median;
                const double noise = base->median == 0.0 ? 0.0 : 100.0 * options.mad_multiplier * base->mad / base->median;
                const int regression = relative > options.threshold && relative > noise;
                const int written = snprintf(fragment, sizeof(fragment), ",\"candidate_median_ns_per_call\":%.9f,\"baseline_median_ns_per_call\":%.9f,\"relative_delta_pct\":%.9f,\"threshold_pct\":%.9f,\"noise_floor_pct\":%.9f,\"regression\":%s}", candidate[index].median, base->median, relative, options.threshold, noise, regression ? "true" : "false");
                if (written < 0 || (size_t)written >= sizeof(fragment) ||
                    (comparisons++ != 0 && json_writer_write_raw(&writer, ",") != JSON_STATUS_SUCCESS) ||
                    json_writer_write_raw(&writer, "{\"profile_id\":") != JSON_STATUS_SUCCESS ||
                    json_writer_write_string(&writer, candidate[index].profile_id) != JSON_STATUS_SUCCESS ||
                    json_writer_write_raw(&writer, ",\"mode\":") != JSON_STATUS_SUCCESS ||
                    json_writer_write_string(&writer, candidate[index].mode) != JSON_STATUS_SUCCESS ||
                    json_writer_write_raw(&writer, fragment) != JSON_STATUS_SUCCESS) goto io_error;
                if (regression) ++regressions;
            }
        }
        /* Symmetric check prevents a candidate from silently dropping a baseline workload. */
        for (size_t index = 0U; index < baseline_count; ++index) {
            if (find_group(candidate, candidate_count, &baseline[index]) < 0) ++missing;
        }
    }
    if (snprintf(fragment, sizeof(fragment), "],\"missing_profiles\":%d,\"regressions\":%d}\n", missing, regressions) < 0 ||
        json_writer_write_raw(&writer, fragment) != JSON_STATUS_SUCCESS ||
        json_writer_commit(&writer, error, sizeof(error)) != JSON_STATUS_SUCCESS) goto io_error;
    groups_destroy(candidate, candidate_count);
    groups_destroy(baseline, baseline_count);
    if (options.baseline == NULL) printf("benchmark_stats: wrote metrics for %zu profile/mode groups to %s\n", candidate_count, options.output);
    else if (missing != 0 || (regressions != 0 && !options.allow_regressions)) fprintf(stderr, "benchmark_stats: regressions=%d missing_profiles=%d; see %s\n", regressions, missing, options.output);
    else printf("benchmark_stats: no regression across %zu profile/mode groups\n", candidate_count);
    return missing != 0 || (regressions != 0 && !options.allow_regressions) ? 1 : 0;

io_error:
    fprintf(stderr, "benchmark_stats: %s\n", error);
    (void)json_writer_abort(&writer);
    groups_destroy(candidate, candidate_count);
    groups_destroy(baseline, baseline_count);
    return 2;
}
