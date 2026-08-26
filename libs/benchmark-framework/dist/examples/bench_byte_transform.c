#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "benchmark_core.h"

typedef struct {
    uint8_t bytes[64];
    size_t used;
} byte_transform_state_t;

static uint64_t next_value(uint64_t value)
{
    value ^= value << 7U;
    value ^= value >> 9U;
    value ^= value << 8U;
    return value;
}

static size_t select_size(const benchmark_workload_t *workload)
{
    if (strcmp(workload->size_profile, "tiny") == 0) return 1U;
    if (strcmp(workload->size_profile, "small") == 0) return 8U;
    if (strcmp(workload->size_profile, "medium") == 0) return 32U;
    if (strcmp(workload->size_profile, "large") == 0 ||
        strcmp(workload->size_profile, "near-capacity") == 0 ||
        strcmp(workload->capacity_profile, "near-capacity") == 0) return 64U;
    return 16U;
}

static benchmark_adapter_status_t initialize_state(
    void *opaque,
    uint64_t sequence_index,
    const benchmark_workload_t *workload,
    void *context)
{
    byte_transform_state_t *state = opaque;
    uint64_t value = workload->seed ^ sequence_index;
    const int zero = strcmp(workload->input_kind, "zero") == 0 ||
        (strcmp(workload->input_kind, "mixed") == 0 && (sequence_index % 2U) == 0U);

    (void)context;
    memset(state, 0, sizeof(*state));
    state->used = select_size(workload);
    if (zero) return 0;
    for (size_t index = 0U; index < state->used; ++index) {
        value = next_value(value);
        state->bytes[index] = (uint8_t)value;
        if (state->bytes[index] == 0U) state->bytes[index] = UINT8_C(1);
    }
    return BENCHMARK_ADAPTER_STATUS_SUCCESS;
}

static benchmark_adapter_status_t transform_state(
    void *opaque,
    uint64_t iteration,
    const benchmark_workload_t *workload,
    void *context)
{
    byte_transform_state_t *state = opaque;

    (void)context;
    if (strcmp(workload->operation_kind, "noop") == 0) return BENCHMARK_ADAPTER_STATUS_SUCCESS;
    for (size_t index = 0U; index < state->used; ++index) {
        const uint8_t mask = (uint8_t)(iteration + index + 1U);
        if (strcmp(workload->operation_kind, "rotate") == 0) {
            state->bytes[index] = (uint8_t)((state->bytes[index] << 1U) |
                (state->bytes[index] >> 7U));
        } else {
            state->bytes[index] ^= mask;
        }
    }
    return BENCHMARK_ADAPTER_STATUS_SUCCESS;
}

static uint64_t checksum_state(const void *opaque, uint64_t iteration, void *context)
{
    const byte_transform_state_t *state = opaque;
    uint64_t hash = UINT64_C(1469598103934665603) ^ iteration;

    (void)context;
    for (size_t index = 0U; index < state->used; ++index) {
        hash ^= state->bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash ^ (uint64_t)state->used;
}

int main(int argc, char **argv)
{
    const benchmark_adapter_t adapter = {
        .benchmark_name = "byte_transform",
        .state_size = sizeof(byte_transform_state_t),
        .success_code = BENCHMARK_ADAPTER_STATUS_SUCCESS,
        .adapter_context = NULL,
        .initialize = initialize_state,
        .operation = transform_state,
        .checksum = checksum_state
    };

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--threads") == 0) {
            return benchmark_core_run_mt(argc, argv, &adapter) == BENCHMARK_CORE_STATUS_SUCCESS ? 0 : 1;
        }
    }
    return benchmark_core_run_st(argc, argv, &adapter) == BENCHMARK_CORE_STATUS_SUCCESS ? 0 : 1;
}
