#ifndef CAN_ANOMALY_H
#define CAN_ANOMALY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_AI_MAX_PROFILES       (32U)
#define CAN_AI_MAX_DATA_BYTES     (64U)
#define CAN_AI_DLC_CODE_COUNT     (16U)
#define CAN_AI_RATE_BUCKET_COUNT  (10U)
#define CAN_AI_INVALID_PROFILE    (0xFFU)

typedef enum e_can_ai_id_mode
{
    CAN_AI_ID_STANDARD = 0,
    CAN_AI_ID_EXTENDED = 1
} can_ai_id_mode_t;

typedef enum e_can_ai_mode
{
    CAN_AI_MODE_LEARNING = 0,
    CAN_AI_MODE_MONITORING
} can_ai_mode_t;

typedef enum e_can_ai_status
{
    CAN_AI_STATUS_OK = 0,
    CAN_AI_STATUS_INVALID_ARGUMENT,
    CAN_AI_STATUS_PROFILE_LIMIT
} can_ai_status_t;

enum
{
    CAN_AI_REASON_NONE          = 0U,
    CAN_AI_REASON_UNKNOWN_ID    = (1UL << 0),
    CAN_AI_REASON_DLC_CHANGED   = (1UL << 1),
    CAN_AI_REASON_PERIOD_FAST   = (1UL << 2),
    CAN_AI_REASON_PERIOD_SLOW   = (1UL << 3),
    CAN_AI_REASON_PAYLOAD_JUMP  = (1UL << 4),
    CAN_AI_REASON_GLOBAL_FLOOD  = (1UL << 5)
};

/**
 * Input frame. dlc is the raw CAN/CAN-FD DLC code (0..15), not a byte count.
 * Drivers that report payload bytes should use can_ai_length_to_dlc(). data may
 * be NULL only when dlc is zero. timestamp_us must be monotonic.
 */
typedef struct st_can_ai_frame
{
    uint32_t         id;
    can_ai_id_mode_t id_mode;
    uint8_t          dlc;
    const uint8_t  * data;
    uint64_t         timestamp_us;
} can_ai_frame_t;

typedef struct st_can_ai_result
{
    uint16_t score;                    /* 0 (normal) .. 1000 (most severe). */
    uint32_t reasons;                  /* CAN_AI_REASON_* bit mask. */
    uint8_t  profile_index;            /* CAN_AI_INVALID_PROFILE for unknown ID. */
    uint8_t  payload_anomaly_bytes;
    uint32_t current_rate_fps;
} can_ai_result_t;

typedef struct st_can_ai_config
{
    uint16_t min_period_samples;       /* Period samples required before timing checks. */
    uint16_t min_payload_samples;      /* Per-byte samples required before payload checks. */

    float    period_sigma;             /* Timing deviation threshold in standard deviations. */
    float    period_jitter_floor_ratio;/* Minimum sigma as a fraction of mean period. */
    uint32_t period_jitter_floor_us;   /* Absolute minimum timing sigma. */
    float    period_fast_ratio;        /* Always fast below mean * this ratio. */
    float    period_slow_ratio;        /* Always slow above mean * this ratio. */

    float    payload_sigma;            /* Per-byte deviation threshold in standard deviations. */
    float    payload_sigma_floor;      /* Minimum per-byte sigma in raw byte units. */
    uint8_t  payload_min_jump;         /* Ignore smaller absolute byte changes. */

    uint32_t rate_window_us;           /* Sliding flood window; normally 1 second. */
    float    flood_multiplier;         /* Learned peak/mean rate multiplier. */
    uint32_t flood_rate_margin_fps;    /* Minimum increase over learned rate. */
    uint32_t flood_min_fps;            /* Never declare a flood below this rate. */
} can_ai_config_t;

typedef struct st_can_ai_profile_view
{
    uint32_t         id;
    can_ai_id_mode_t id_mode;
    uint8_t          expected_dlc;
    uint8_t          expected_length;
    uint32_t         learning_frames;
    uint64_t         observed_frames;
    uint64_t         anomaly_frames;
    uint64_t         last_seen_us;
    float            mean_period_us;
    float            period_variance_us2;
    uint16_t         last_score;
    uint32_t         last_reasons;
} can_ai_profile_view_t;

typedef struct st_can_ai_stats
{
    can_ai_mode_t mode;
    uint8_t       profile_count;
    uint64_t      total_frames;
    uint64_t      anomaly_frames;
    uint32_t      unknown_frames;
    uint32_t      model_full_frames;
    uint32_t      current_rate_fps;
    uint32_t      learned_rate_fps;
    uint32_t      learned_peak_rate_fps;
    uint32_t      flood_threshold_fps;
    uint16_t      last_score;
    uint32_t      last_reasons;
} can_ai_stats_t;

/* Public so the detector can be allocated statically; applications should not edit it. */
typedef struct st_can_ai_running_stat
{
    uint32_t count;
    float    mean;
    float    m2;
} can_ai_running_stat_t;

typedef struct st_can_ai_profile_state
{
    uint32_t              id;
    can_ai_id_mode_t      id_mode;
    uint8_t               expected_dlc;
    uint8_t               period_reference_valid;
    uint32_t              dlc_histogram[CAN_AI_DLC_CODE_COUNT];
    uint32_t              learning_frames;
    uint64_t              observed_frames;
    uint64_t              anomaly_frames;
    uint64_t              last_seen_us;
    can_ai_running_stat_t period;
    can_ai_running_stat_t payload[CAN_AI_MAX_DATA_BYTES];
    uint16_t              last_score;
    uint32_t              last_reasons;
} can_ai_profile_state_t;

typedef struct st_can_ai_detector
{
    can_ai_config_t       config;
    can_ai_mode_t         mode;
    uint8_t               profile_count;
    can_ai_profile_state_t profiles[CAN_AI_MAX_PROFILES];

    uint64_t total_frames;
    uint64_t anomaly_frames;
    uint32_t unknown_frames;
    uint32_t model_full_frames;
    uint16_t last_score;
    uint32_t last_reasons;

    uint64_t learning_first_us;
    uint64_t learning_last_us;
    uint64_t learning_frames;
    uint32_t learned_rate_fps;
    uint32_t learned_peak_rate_fps;
    uint32_t flood_threshold_fps;

    uint32_t rate_buckets[CAN_AI_RATE_BUCKET_COUNT];
    uint64_t rate_bucket_number;
    uint32_t rate_total;
    uint8_t  rate_valid;
} can_ai_detector_t;

/** Fill a configuration with conservative defaults for a periodic industrial bus. */
void can_ai_default_config(can_ai_config_t * config);

/** Initialize and enter learning. Non-finite float settings fall back to defaults;
 * config may safely point to detector->config when resetting with existing settings.
 */
void can_ai_init(can_ai_detector_t * detector, const can_ai_config_t * config);

/** Erase all profiles/statistics and start a new learning phase. */
void can_ai_start_learning(can_ai_detector_t * detector);

/** Freeze the learned model and enter monitoring. Returns false for an empty model. */
bool can_ai_finish_learning(can_ai_detector_t * detector);

/** Process one frame. Call from the main loop, not concurrently from an ISR. */
can_ai_status_t can_ai_process(can_ai_detector_t * detector,
                               const can_ai_frame_t * frame,
                               can_ai_result_t * result);

/** Convert a raw CAN-FD DLC code (0..15) to payload bytes (0..64). */
uint8_t can_ai_dlc_to_length(uint8_t dlc);

/** Convert a legal payload byte length to raw DLC; returns 0xFF when invalid. */
uint8_t can_ai_length_to_dlc(uint8_t length);

void   can_ai_get_stats(const can_ai_detector_t * detector, can_ai_stats_t * stats);
size_t can_ai_get_profile_count(const can_ai_detector_t * detector);
bool   can_ai_get_profile(const can_ai_detector_t * detector,
                          size_t index,
                          can_ai_profile_view_t * profile);

/** Copy the busiest profiles, descending by observed frame count. */
size_t can_ai_get_top_profiles(const can_ai_detector_t * detector,
                               can_ai_profile_view_t * profiles,
                               size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* CAN_ANOMALY_H */
