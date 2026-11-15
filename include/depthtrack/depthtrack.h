#ifndef DEPTHTRACK_C_API_H
#define DEPTHTRACK_C_API_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && defined(DEPTHTRACK_SHARED)
# if defined(DEPTHTRACK_BUILD)
#  define DT_API __declspec(dllexport)
# else
#  define DT_API __declspec(dllimport)
# endif
#else
# define DT_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DT_ABI_VERSION 1u
/* All coordinates are millimeters; timestamps and durations are seconds.
   One handle tracks one semantic point and is not thread safe. Different
   handles may be used independently. Returned structs/history are copies. */
typedef struct dt_tracker dt_tracker;
typedef struct dt_point { double x, y, z; } dt_point;
typedef struct dt_parameters {
    double min_cutoff_hz, speed_beta, derivative_cutoff_hz, max_cutoff_hz;
    double jump_slack_mm, max_speed_mm_s;
    double hold_seconds, reset_gap_seconds, trail_seconds;
} dt_parameters;
typedef enum dt_error {
    DT_OK=0, DT_INVALID_ARGUMENT=1, DT_INVALID_PARAMETERS=2,
    DT_INVALID_TIMESTAMP=3, DT_BUFFER_TOO_SMALL=4,
    DT_ALLOCATION_FAILED=5, DT_INTERNAL_ERROR=6, DT_NUMERICAL_RANGE=7
} dt_error;
typedef enum dt_status {
    DT_LOST=0, DT_INITIALIZED=1, DT_UPDATED=2, DT_HELD=3, DT_REACQUIRED=4
} dt_status;
typedef enum dt_reason {
    DT_REASON_NONE=0, DT_REASON_MISSING=1, DT_REASON_INVALID=2, DT_REASON_JUMP=3
} dt_reason;
typedef struct dt_state {
    uint32_t status, reason, accepted, has_position;
    dt_point position; /* zero when has_position == 0 */
    uint32_t has_observation_age, has_cutoff;
    double observation_age_s, cutoff_hz; /* zero when absent */
    uint64_t generation;
    size_t history_count;
} dt_state;
typedef struct dt_history_sample { double time_s; dt_point position; } dt_history_sample;
DT_API uint32_t dt_abi_version(void);
DT_API void dt_parameters_default(dt_parameters* out);
/* NULL parameters selects defaults. NULL error_out is allowed. */
DT_API dt_tracker* dt_tracker_create(const dt_parameters* parameters, dt_error* error_out);
DT_API void dt_tracker_destroy(dt_tracker* tracker); /* NULL is harmless. */
DT_API dt_error dt_tracker_reset(dt_tracker* tracker);
/* NULL observation is missing. Nonfinite XYZ or z<=0 consumes the timestamp
   as a missing/invalid observation, matching the Python reference. Invalid
   timestamps/API arguments leave tracker and output unchanged. Computation
   overflow returns DT_NUMERICAL_RANGE atomically (additional native safeguard). */
DT_API dt_error dt_tracker_update(dt_tracker* tracker, double time_s,
                                 const dt_point* observation, dt_state* out);
DT_API size_t dt_tracker_history_size(const dt_tracker* tracker);
/* Copies all samples oldest first. On insufficient capacity, writes required
   count only and returns DT_BUFFER_TOO_SMALL, leaving the destination untouched. */
DT_API dt_error dt_tracker_history_copy(const dt_tracker* tracker,
    dt_history_sample* destination, size_t capacity, size_t* written);
DT_API const char* dt_error_string(dt_error error);
DT_API const char* dt_status_string(uint32_t status);
DT_API const char* dt_reason_string(uint32_t reason);
#ifdef __cplusplus
}
#endif
#endif
