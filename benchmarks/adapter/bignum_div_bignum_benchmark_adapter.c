#include "bignum_div_bignum_benchmark_adapter.h"
#include "bignum_div_bignum.h"
#include <string.h>
#include <stdint.h>
#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)
typedef struct {
    bignum_t a;
    bignum_t b;
    bignum_t result;
    bignum_t remainder;
} div_bignum_state_t;
static int equal_text(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static uint64_t next_value(uint64_t *s) { if (*s == 0U) *s = UINT64_C(0x9e3779b97f4a7c15); *s ^= *s << 7U; *s ^= *s >> 9U; *s ^= *s << 8U; return *s; }
static size_t choose_length(const benchmark_workload_t *w) { if (equal_text(w->size_profile, "one") || equal_text(w->size_profile, "tiny")) return 1U; if (equal_text(w->size_profile, "quarter") || equal_text(w->size_profile, "small")) return BIGNUM_CAPACITY/4U; if (equal_text(w->size_profile, "half") || equal_text(w->size_profile, "medium")) return BIGNUM_CAPACITY/2U; return BIGNUM_CAPACITY; }
static benchmark_adapter_status_t initialize(void *opaque, uint64_t index, const benchmark_workload_t *w, void *ctx) {
    div_bignum_state_t *state = opaque; uint64_t seed; size_t n; (void)ctx;
    if (state == NULL || w == NULL || bignum_div_bignum_benchmark_validate_workload(w) != BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS) return BENCHMARK_ADAPTER_STATUS_INPUT_ERROR;
    memset(state, 0, sizeof(*state)); seed = w->seed ^ (index + UINT64_C(0x9e3779b97f4a7c15)); n = choose_length(w); state->a.len = n;
    for (size_t i = 0U; i < n; ++i) state->a.words[i] = next_value(&seed);
    if (state->a.words[n - 1U] == 0U) state->a.words[n - 1U] = 1U;
    state->b.len = state->a.len;
    for (size_t i = 0U; i < state->b.len; ++i) state->b.words[i] = i == state->b.len - 1U ? 3U : 0U;
    return BENCHMARK_ADAPTER_STATUS_SUCCESS; }
static benchmark_adapter_status_t operation(void *opaque, uint64_t iteration, const benchmark_workload_t *w, void *ctx) { div_bignum_state_t *state = opaque; (void)iteration; (void)w; (void)ctx;
    if (bignum_div_bignum(&state->a, &state->b, &state->result, &state->remainder) != BIGNUM_DIV_BIGNUM_OK) return BENCHMARK_ADAPTER_STATUS_OPERATION_ERROR;
    return BENCHMARK_ADAPTER_STATUS_SUCCESS; }
static uint64_t checksum(const void *opaque, uint64_t iteration, void *ctx) { const div_bignum_state_t *s = opaque; uint64_t h = FNV_OFFSET; (void)ctx; if (s == NULL) return 0U; for (size_t i = 0U; i < BIGNUM_CAPACITY; ++i) { h ^= s->result.words[i]; h *= FNV_PRIME; } h ^= s->result.len; h *= FNV_PRIME; return h ^ iteration; }
bignum_div_bignum_benchmark_status_t bignum_div_bignum_benchmark_validate_workload(const benchmark_workload_t *w) { if (w == NULL) return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_NULL_ARGUMENT; if (equal_text(w->operation_kind, "xor")) return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_INVALID_PROFILE; if (w->input_kind == NULL || w->operation_kind == NULL || w->measure_mode == NULL || w->size_profile == NULL || w->capacity_profile == NULL) return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_INVALID_PROFILE; return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS; }
bignum_div_bignum_benchmark_status_t bignum_div_bignum_benchmark_adapter_init(benchmark_adapter_t *adapter) { if (adapter == NULL) return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_NULL_ARGUMENT; *adapter = (benchmark_adapter_t){ .benchmark_name = "bignum_div_bignum", .state_size = sizeof(div_bignum_state_t), .success_code = BENCHMARK_ADAPTER_STATUS_SUCCESS, .adapter_context = NULL, .initialize = initialize, .operation = operation, .checksum = checksum }; return BIGNUM_DIV_BIGNUM_BENCHMARK_STATUS_SUCCESS; }
