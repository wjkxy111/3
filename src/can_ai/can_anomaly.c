#include "can_anomaly.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define CAN_AI_SCORE_MAX                 (1000U)
#define CAN_AI_SCORE_UNKNOWN_ID          (650U)
#define CAN_AI_SCORE_DLC_CHANGED         (300U)
#define CAN_AI_SCORE_PERIOD_FAST         (260U)
#define CAN_AI_SCORE_PERIOD_SLOW         (220U)
#define CAN_AI_SCORE_PAYLOAD_BASE        (160U)
#define CAN_AI_SCORE_PAYLOAD_PER_BYTE    (30U)
#define CAN_AI_SCORE_PAYLOAD_MAX         (400U)
#define CAN_AI_SCORE_GLOBAL_FLOOD        (400U)

static const uint8_t s_dlc_to_length[CAN_AI_DLC_CODE_COUNT] =
{
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
    8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
};

static uint32_t can_ai_sat_inc_u32(uint32_t value)
{
    return (UINT32_MAX == value) ? value : (value + 1U);
}

static uint64_t can_ai_sat_inc_u64(uint64_t value)
{
    return (UINT64_MAX == value) ? value : (value + 1U);
}

static uint16_t can_ai_score_add(uint16_t score, uint32_t addition)
{
    uint32_t sum = (uint32_t) score + addition;

    if (sum > CAN_AI_SCORE_MAX)
    {
        sum = CAN_AI_SCORE_MAX;
    }

    return (uint16_t) sum;
}

static void can_ai_running_stat_push(can_ai_running_stat_t * stat, float sample)
{
    float delta;
    float delta_after;

    if ((NULL == stat) || (UINT32_MAX == stat->count))
    {
        return;
    }

    stat->count++;
    delta       = sample - stat->mean;
    stat->mean += delta / (float) stat->count;
    delta_after = sample - stat->mean;
    stat->m2   += delta * delta_after;

    /* Round-off can make M2 very slightly negative for a constant signal. */
    if (stat->m2 < 0.0F)
    {
        stat->m2 = 0.0F;
    }
}

static float can_ai_running_stat_variance(const can_ai_running_stat_t * stat)
{
    if ((NULL == stat) || (stat->count < 2U))
    {
        return 0.0F;
    }

    return stat->m2 / (float) (stat->count - 1U);
}

static void can_ai_rate_reset(can_ai_detector_t * detector)
{
    memset(detector->rate_buckets, 0, sizeof(detector->rate_buckets));
    detector->rate_bucket_number = 0U;
    detector->rate_total         = 0U;
    detector->rate_valid         = 0U;
}

static uint64_t can_ai_rate_bucket_width_us(const can_ai_detector_t * detector)
{
    uint64_t width = detector->config.rate_window_us / CAN_AI_RATE_BUCKET_COUNT;

    return (0U == width) ? 1U : width;
}

static void can_ai_rate_update(can_ai_detector_t * detector, uint64_t timestamp_us)
{
    uint64_t bucket_number = timestamp_us / can_ai_rate_bucket_width_us(detector);
    uint32_t bucket_index;

    if (0U == detector->rate_valid)
    {
        can_ai_rate_reset(detector);
        detector->rate_bucket_number = bucket_number;
        detector->rate_valid         = 1U;
    }
    else if (bucket_number < detector->rate_bucket_number)
    {
        /* A restarted timebase must not preserve stale traffic as a false flood. */
        can_ai_rate_reset(detector);
        detector->rate_bucket_number = bucket_number;
        detector->rate_valid         = 1U;
    }
    else if (bucket_number > detector->rate_bucket_number)
    {
        uint64_t elapsed_buckets = bucket_number - detector->rate_bucket_number;

        if (elapsed_buckets >= CAN_AI_RATE_BUCKET_COUNT)
        {
            memset(detector->rate_buckets, 0, sizeof(detector->rate_buckets));
            detector->rate_total = 0U;
        }
        else
        {
            uint64_t bucket;

            for (bucket = detector->rate_bucket_number + 1U;
                 bucket <= bucket_number;
                 bucket++)
            {
                uint32_t expired_index = (uint32_t) (bucket % CAN_AI_RATE_BUCKET_COUNT);

                detector->rate_total -= detector->rate_buckets[expired_index];
                detector->rate_buckets[expired_index] = 0U;
            }
        }

        detector->rate_bucket_number = bucket_number;
    }

    bucket_index = (uint32_t) (bucket_number % CAN_AI_RATE_BUCKET_COUNT);
    detector->rate_buckets[bucket_index] = can_ai_sat_inc_u32(detector->rate_buckets[bucket_index]);
    detector->rate_total                 = can_ai_sat_inc_u32(detector->rate_total);
}

static uint32_t can_ai_current_rate_fps(const can_ai_detector_t * detector)
{
    uint64_t scaled_rate;

    if ((0U == detector->rate_valid) || (0U == detector->config.rate_window_us))
    {
        return 0U;
    }

    scaled_rate = (uint64_t) detector->rate_total * 1000000ULL;
    scaled_rate = (scaled_rate + (detector->config.rate_window_us / 2U)) /
                  detector->config.rate_window_us;

    return (scaled_rate > UINT32_MAX) ? UINT32_MAX : (uint32_t) scaled_rate;
}

static int32_t can_ai_find_profile(const can_ai_detector_t * detector,
                                   uint32_t id,
                                   can_ai_id_mode_t id_mode)
{
    uint8_t index;

    for (index = 0U; index < detector->profile_count; index++)
    {
        const can_ai_profile_state_t * profile = &detector->profiles[index];

        if ((profile->id == id) && (profile->id_mode == id_mode))
        {
            return (int32_t) index;
        }
    }

    return -1;
}

static int32_t can_ai_create_profile(can_ai_detector_t * detector,
                                     uint32_t id,
                                     can_ai_id_mode_t id_mode)
{
    can_ai_profile_state_t * profile;
    uint8_t index;

    if (detector->profile_count >= CAN_AI_MAX_PROFILES)
    {
        return -1;
    }

    index   = detector->profile_count;
    profile = &detector->profiles[index];
    memset(profile, 0, sizeof(*profile));
    profile->id      = id;
    profile->id_mode = id_mode;
    detector->profile_count++;

    return (int32_t) index;
}

static uint8_t can_ai_histogram_mode(const uint32_t histogram[CAN_AI_DLC_CODE_COUNT])
{
    uint8_t best = 0U;
    uint8_t index;

    for (index = 1U; index < CAN_AI_DLC_CODE_COUNT; index++)
    {
        if (histogram[index] > histogram[best])
        {
            best = index;
        }
    }

    return best;
}

static bool can_ai_frame_is_valid(const can_ai_frame_t * frame)
{
    uint8_t length;

    if ((NULL == frame) || (frame->dlc >= CAN_AI_DLC_CODE_COUNT))
    {
        return false;
    }

    if (((CAN_AI_ID_STANDARD != frame->id_mode) && (CAN_AI_ID_EXTENDED != frame->id_mode)) ||
        ((CAN_AI_ID_STANDARD == frame->id_mode) && (frame->id > 0x7FFU)) ||
        ((CAN_AI_ID_EXTENDED == frame->id_mode) && (frame->id > 0x1FFFFFFFU)))
    {
        return false;
    }

    length = can_ai_dlc_to_length(frame->dlc);

    return (0U == length) || (NULL != frame->data);
}

static void can_ai_learn_profile(can_ai_profile_state_t * profile,
                                 const can_ai_frame_t * frame)
{
    uint8_t length = can_ai_dlc_to_length(frame->dlc);
    uint8_t byte_index;

    profile->learning_frames = can_ai_sat_inc_u32(profile->learning_frames);
    profile->observed_frames = can_ai_sat_inc_u64(profile->observed_frames);
    profile->dlc_histogram[frame->dlc] = can_ai_sat_inc_u32(profile->dlc_histogram[frame->dlc]);
    if (profile->dlc_histogram[frame->dlc] > profile->dlc_histogram[profile->expected_dlc])
    {
        profile->expected_dlc = frame->dlc;
    }

    if (0U != profile->period_reference_valid)
    {
        if (frame->timestamp_us >= profile->last_seen_us)
        {
            uint64_t period_us = frame->timestamp_us - profile->last_seen_us;
            float period_sample = (period_us > UINT32_MAX) ? (float) UINT32_MAX : (float) period_us;

            can_ai_running_stat_push(&profile->period, period_sample);
        }
    }

    profile->last_seen_us            = frame->timestamp_us;
    profile->period_reference_valid  = 1U;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        can_ai_running_stat_push(&profile->payload[byte_index], (float) frame->data[byte_index]);
    }
}

static bool can_ai_period_deviation(const can_ai_detector_t * detector,
                                    const can_ai_profile_state_t * profile,
                                    float period_us,
                                    bool * too_fast)
{
    float mean;
    float difference;
    float variance;
    float jitter_floor;
    float jitter_floor_ratio;
    float variance_floor;
    float sigma_limit_squared;
    bool sigma_deviation;

    if ((profile->period.count < detector->config.min_period_samples) ||
        (profile->period.mean <= 0.0F))
    {
        return false;
    }

    mean                  = profile->period.mean;
    difference            = period_us - mean;
    variance              = can_ai_running_stat_variance(&profile->period);
    jitter_floor          = (float) detector->config.period_jitter_floor_us;
    jitter_floor_ratio    = mean * detector->config.period_jitter_floor_ratio;

    if (jitter_floor_ratio > jitter_floor)
    {
        jitter_floor = jitter_floor_ratio;
    }

    variance_floor = jitter_floor * jitter_floor;
    if (variance < variance_floor)
    {
        variance = variance_floor;
    }

    sigma_limit_squared = detector->config.period_sigma * detector->config.period_sigma * variance;
    sigma_deviation     = ((difference * difference) > sigma_limit_squared);

    if (period_us < mean)
    {
        *too_fast = true;

        return (period_us < (mean * detector->config.period_fast_ratio)) || sigma_deviation;
    }

    *too_fast = false;

    return (period_us > (mean * detector->config.period_slow_ratio)) || sigma_deviation;
}

static uint8_t can_ai_payload_anomalies(const can_ai_detector_t * detector,
                                        const can_ai_profile_state_t * profile,
                                        const can_ai_frame_t * frame)
{
    uint8_t actual_length   = can_ai_dlc_to_length(frame->dlc);
    uint8_t expected_length = can_ai_dlc_to_length(profile->expected_dlc);
    uint8_t compare_length  = (actual_length < expected_length) ? actual_length : expected_length;
    uint8_t anomaly_bytes   = 0U;
    uint8_t byte_index;

    for (byte_index = 0U; byte_index < compare_length; byte_index++)
    {
        const can_ai_running_stat_t * byte_stat = &profile->payload[byte_index];
        float difference;
        float absolute_difference;
        float variance;
        float variance_floor;
        float sigma_limit_squared;

        if (byte_stat->count < detector->config.min_payload_samples)
        {
            continue;
        }

        difference          = (float) frame->data[byte_index] - byte_stat->mean;
        absolute_difference = (difference < 0.0F) ? -difference : difference;

        if (absolute_difference < (float) detector->config.payload_min_jump)
        {
            continue;
        }

        variance       = can_ai_running_stat_variance(byte_stat);
        variance_floor = detector->config.payload_sigma_floor *
                         detector->config.payload_sigma_floor;
        if (variance < variance_floor)
        {
            variance = variance_floor;
        }

        sigma_limit_squared = detector->config.payload_sigma *
                              detector->config.payload_sigma * variance;
        if ((difference * difference) > sigma_limit_squared)
        {
            anomaly_bytes++;
        }
    }

    return anomaly_bytes;
}

static void can_ai_profile_to_view(const can_ai_profile_state_t * source,
                                   can_ai_profile_view_t * destination)
{
    destination->id                  = source->id;
    destination->id_mode             = source->id_mode;
    destination->expected_dlc        = source->expected_dlc;
    destination->expected_length     = can_ai_dlc_to_length(source->expected_dlc);
    destination->learning_frames     = source->learning_frames;
    destination->observed_frames     = source->observed_frames;
    destination->anomaly_frames      = source->anomaly_frames;
    destination->last_seen_us        = source->last_seen_us;
    destination->mean_period_us      = source->period.mean;
    destination->period_variance_us2 = can_ai_running_stat_variance(&source->period);
    destination->last_score          = source->last_score;
    destination->last_reasons        = source->last_reasons;
}

void can_ai_default_config(can_ai_config_t * config)
{
    if (NULL == config)
    {
        return;
    }

    config->min_period_samples        = 8U;
    config->min_payload_samples       = 12U;
    config->period_sigma              = 4.0F;
    config->period_jitter_floor_ratio = 0.08F;
    config->period_jitter_floor_us    = 100U;
    config->period_fast_ratio         = 0.35F;
    config->period_slow_ratio         = 2.50F;
    config->payload_sigma             = 5.0F;
    config->payload_sigma_floor       = 2.0F;
    config->payload_min_jump          = 12U;
    config->rate_window_us            = 1000000U;
    config->flood_multiplier          = 2.0F;
    config->flood_rate_margin_fps     = 50U;
    config->flood_min_fps             = 200U;
}

void can_ai_init(can_ai_detector_t * detector, const can_ai_config_t * config)
{
    can_ai_config_t defaults;

    if (NULL == detector)
    {
        return;
    }

    can_ai_default_config(&defaults);
    /* Copy before clearing: config may point to detector->config. */
    can_ai_config_t selected = (NULL == config) ? defaults : *config;
    memset(detector, 0, sizeof(*detector));
    detector->config = selected;

    if (0U == detector->config.min_period_samples)
    {
        detector->config.min_period_samples = defaults.min_period_samples;
    }
    if (0U == detector->config.min_payload_samples)
    {
        detector->config.min_payload_samples = defaults.min_payload_samples;
    }
    if (!isfinite(detector->config.period_sigma) || (detector->config.period_sigma <= 0.0F))
    {
        detector->config.period_sigma = defaults.period_sigma;
    }
    if (!isfinite(detector->config.period_jitter_floor_ratio) ||
        (detector->config.period_jitter_floor_ratio < 0.0F) ||
        (detector->config.period_jitter_floor_ratio >= 1.0F))
    {
        detector->config.period_jitter_floor_ratio = defaults.period_jitter_floor_ratio;
    }
    if (!isfinite(detector->config.period_fast_ratio) ||
        (detector->config.period_fast_ratio <= 0.0F) ||
        (detector->config.period_fast_ratio >= 1.0F))
    {
        detector->config.period_fast_ratio = defaults.period_fast_ratio;
    }
    if (!isfinite(detector->config.period_slow_ratio) || (detector->config.period_slow_ratio <= 1.0F))
    {
        detector->config.period_slow_ratio = defaults.period_slow_ratio;
    }
    if (!isfinite(detector->config.payload_sigma) || (detector->config.payload_sigma <= 0.0F))
    {
        detector->config.payload_sigma = defaults.payload_sigma;
    }
    if (!isfinite(detector->config.payload_sigma_floor) || (detector->config.payload_sigma_floor < 0.0F))
    {
        detector->config.payload_sigma_floor = defaults.payload_sigma_floor;
    }
    if (0U == detector->config.rate_window_us)
    {
        detector->config.rate_window_us = defaults.rate_window_us;
    }
    if (!isfinite(detector->config.flood_multiplier) || (detector->config.flood_multiplier <= 1.0F))
    {
        detector->config.flood_multiplier = defaults.flood_multiplier;
    }

    detector->mode = CAN_AI_MODE_LEARNING;
}

void can_ai_start_learning(can_ai_detector_t * detector)
{
    can_ai_config_t config;

    if (NULL == detector)
    {
        return;
    }

    config = detector->config;
    memset(detector, 0, sizeof(*detector));
    detector->config = config;
    detector->mode   = CAN_AI_MODE_LEARNING;
}

bool can_ai_finish_learning(can_ai_detector_t * detector)
{
    uint32_t base_rate;
    uint32_t threshold;
    uint8_t index;

    if ((NULL == detector) || (0U == detector->profile_count))
    {
        return false;
    }

    if ((detector->learning_frames > 1U) &&
        (detector->learning_last_us > detector->learning_first_us))
    {
        uint64_t duration = detector->learning_last_us - detector->learning_first_us;
        uint64_t intervals = detector->learning_frames - 1U;
        uint64_t rate;

        if (intervals > (UINT64_MAX / 1000000ULL))
        {
            rate = UINT32_MAX;
        }
        else
        {
            rate = (intervals * 1000000ULL + (duration / 2U)) / duration;
        }

        detector->learned_rate_fps = (rate > UINT32_MAX) ? UINT32_MAX : (uint32_t) rate;
    }

    for (index = 0U; index < detector->profile_count; index++)
    {
        can_ai_profile_state_t * profile = &detector->profiles[index];

        profile->expected_dlc              = can_ai_histogram_mode(profile->dlc_histogram);
        profile->period_reference_valid    = 0U;
        profile->last_score                = 0U;
        profile->last_reasons              = CAN_AI_REASON_NONE;
    }

    base_rate = detector->learned_rate_fps;
    if (detector->learned_peak_rate_fps > base_rate)
    {
        base_rate = detector->learned_peak_rate_fps;
    }

    if (((float) base_rate * detector->config.flood_multiplier) >= (float) UINT32_MAX)
    {
        threshold = UINT32_MAX;
    }
    else
    {
        threshold = (uint32_t) ((float) base_rate * detector->config.flood_multiplier + 0.5F);
    }

    if ((UINT32_MAX - detector->learned_rate_fps) < detector->config.flood_rate_margin_fps)
    {
        threshold = UINT32_MAX;
    }
    else if ((detector->learned_rate_fps + detector->config.flood_rate_margin_fps) > threshold)
    {
        threshold = detector->learned_rate_fps + detector->config.flood_rate_margin_fps;
    }

    if (detector->config.flood_min_fps > threshold)
    {
        threshold = detector->config.flood_min_fps;
    }

    detector->flood_threshold_fps = threshold;
    detector->mode                = CAN_AI_MODE_MONITORING;
    detector->last_score          = 0U;
    detector->last_reasons        = CAN_AI_REASON_NONE;
    can_ai_rate_reset(detector);

    return true;
}

can_ai_status_t can_ai_process(can_ai_detector_t * detector,
                               const can_ai_frame_t * frame,
                               can_ai_result_t * result)
{
    can_ai_result_t local_result;
    int32_t profile_index;
    uint32_t current_rate;

    memset(&local_result, 0, sizeof(local_result));
    local_result.profile_index = CAN_AI_INVALID_PROFILE;

    if ((NULL == detector) || !can_ai_frame_is_valid(frame))
    {
        if (NULL != result)
        {
            *result = local_result;
        }

        return CAN_AI_STATUS_INVALID_ARGUMENT;
    }

    detector->total_frames = can_ai_sat_inc_u64(detector->total_frames);
    can_ai_rate_update(detector, frame->timestamp_us);
    current_rate                 = can_ai_current_rate_fps(detector);
    local_result.current_rate_fps = current_rate;

    profile_index = can_ai_find_profile(detector, frame->id, frame->id_mode);

    if (CAN_AI_MODE_LEARNING == detector->mode)
    {
        if (detector->learning_frames == 0U)
        {
            detector->learning_first_us = frame->timestamp_us;
        }
        if ((detector->learning_frames == 0U) ||
            (frame->timestamp_us >= detector->learning_last_us))
        {
            detector->learning_last_us = frame->timestamp_us;
        }
        detector->learning_frames = can_ai_sat_inc_u64(detector->learning_frames);

        if (current_rate > detector->learned_peak_rate_fps)
        {
            detector->learned_peak_rate_fps = current_rate;
        }

        if (profile_index < 0)
        {
            profile_index = can_ai_create_profile(detector, frame->id, frame->id_mode);
        }

        if (profile_index < 0)
        {
            detector->model_full_frames = can_ai_sat_inc_u32(detector->model_full_frames);
            if (NULL != result)
            {
                *result = local_result;
            }

            return CAN_AI_STATUS_PROFILE_LIMIT;
        }

        local_result.profile_index = (uint8_t) profile_index;
        can_ai_learn_profile(&detector->profiles[profile_index], frame);

        if (NULL != result)
        {
            *result = local_result;
        }

        return CAN_AI_STATUS_OK;
    }

    if ((detector->flood_threshold_fps > 0U) &&
        (current_rate >= detector->flood_threshold_fps))
    {
        local_result.reasons |= CAN_AI_REASON_GLOBAL_FLOOD;
        local_result.score = can_ai_score_add(local_result.score, CAN_AI_SCORE_GLOBAL_FLOOD);
    }

    if (profile_index < 0)
    {
        local_result.reasons |= CAN_AI_REASON_UNKNOWN_ID;
        local_result.score = can_ai_score_add(local_result.score, CAN_AI_SCORE_UNKNOWN_ID);
        detector->unknown_frames = can_ai_sat_inc_u32(detector->unknown_frames);
    }
    else
    {
        can_ai_profile_state_t * profile = &detector->profiles[profile_index];
        uint8_t payload_anomalies;

        local_result.profile_index = (uint8_t) profile_index;
        profile->observed_frames   = can_ai_sat_inc_u64(profile->observed_frames);

        if (frame->dlc != profile->expected_dlc)
        {
            local_result.reasons |= CAN_AI_REASON_DLC_CHANGED;
            local_result.score = can_ai_score_add(local_result.score, CAN_AI_SCORE_DLC_CHANGED);
        }

        if (0U != profile->period_reference_valid)
        {
            if (frame->timestamp_us >= profile->last_seen_us)
            {
                uint64_t period_u64 = frame->timestamp_us - profile->last_seen_us;
                float period_us = (period_u64 > UINT32_MAX) ? (float) UINT32_MAX : (float) period_u64;
                bool too_fast = false;

                if (can_ai_period_deviation(detector, profile, period_us, &too_fast))
                {
                    if (too_fast)
                    {
                        local_result.reasons |= CAN_AI_REASON_PERIOD_FAST;
                        local_result.score = can_ai_score_add(local_result.score, CAN_AI_SCORE_PERIOD_FAST);
                    }
                    else
                    {
                        local_result.reasons |= CAN_AI_REASON_PERIOD_SLOW;
                        local_result.score = can_ai_score_add(local_result.score, CAN_AI_SCORE_PERIOD_SLOW);
                    }
                }
            }
        }

        profile->last_seen_us           = frame->timestamp_us;
        profile->period_reference_valid = 1U;

        payload_anomalies = can_ai_payload_anomalies(detector, profile, frame);
        if (payload_anomalies > 0U)
        {
            uint32_t payload_score = CAN_AI_SCORE_PAYLOAD_BASE +
                                     ((uint32_t) payload_anomalies * CAN_AI_SCORE_PAYLOAD_PER_BYTE);

            if (payload_score > CAN_AI_SCORE_PAYLOAD_MAX)
            {
                payload_score = CAN_AI_SCORE_PAYLOAD_MAX;
            }

            local_result.payload_anomaly_bytes = payload_anomalies;
            local_result.reasons |= CAN_AI_REASON_PAYLOAD_JUMP;
            local_result.score = can_ai_score_add(local_result.score, payload_score);
        }

        profile->last_score   = local_result.score;
        profile->last_reasons = local_result.reasons;
        if (CAN_AI_REASON_NONE != local_result.reasons)
        {
            profile->anomaly_frames = can_ai_sat_inc_u64(profile->anomaly_frames);
        }
    }

    detector->last_score   = local_result.score;
    detector->last_reasons = local_result.reasons;
    if (CAN_AI_REASON_NONE != local_result.reasons)
    {
        detector->anomaly_frames = can_ai_sat_inc_u64(detector->anomaly_frames);
    }

    if (NULL != result)
    {
        *result = local_result;
    }

    return CAN_AI_STATUS_OK;
}

uint8_t can_ai_dlc_to_length(uint8_t dlc)
{
    return (dlc < CAN_AI_DLC_CODE_COUNT) ? s_dlc_to_length[dlc] : 0U;
}

uint8_t can_ai_length_to_dlc(uint8_t length)
{
    uint8_t dlc;

    for (dlc = 0U; dlc < CAN_AI_DLC_CODE_COUNT; dlc++)
    {
        if (s_dlc_to_length[dlc] == length)
        {
            return dlc;
        }
    }

    return 0xFFU;
}

void can_ai_get_stats(const can_ai_detector_t * detector, can_ai_stats_t * stats)
{
    if ((NULL == detector) || (NULL == stats))
    {
        return;
    }

    stats->mode                    = detector->mode;
    stats->profile_count           = detector->profile_count;
    stats->total_frames            = detector->total_frames;
    stats->anomaly_frames          = detector->anomaly_frames;
    stats->unknown_frames          = detector->unknown_frames;
    stats->model_full_frames       = detector->model_full_frames;
    stats->current_rate_fps        = can_ai_current_rate_fps(detector);
    stats->learned_rate_fps        = detector->learned_rate_fps;
    stats->learned_peak_rate_fps   = detector->learned_peak_rate_fps;
    stats->flood_threshold_fps     = detector->flood_threshold_fps;
    stats->last_score              = detector->last_score;
    stats->last_reasons            = detector->last_reasons;
}

size_t can_ai_get_profile_count(const can_ai_detector_t * detector)
{
    return (NULL == detector) ? 0U : detector->profile_count;
}

bool can_ai_get_profile(const can_ai_detector_t * detector,
                        size_t index,
                        can_ai_profile_view_t * profile)
{
    if ((NULL == detector) || (NULL == profile) || (index >= detector->profile_count))
    {
        return false;
    }

    can_ai_profile_to_view(&detector->profiles[index], profile);

    return true;
}

size_t can_ai_get_top_profiles(const can_ai_detector_t * detector,
                               can_ai_profile_view_t * profiles,
                               size_t capacity)
{
    bool selected[CAN_AI_MAX_PROFILES] = {false};
    size_t output_count;
    size_t limit;

    if ((NULL == detector) || (NULL == profiles) || (0U == capacity))
    {
        return 0U;
    }

    limit = detector->profile_count;
    if (capacity < limit)
    {
        limit = capacity;
    }

    for (output_count = 0U; output_count < limit; output_count++)
    {
        size_t candidate;
        size_t best = CAN_AI_MAX_PROFILES;

        for (candidate = 0U; candidate < detector->profile_count; candidate++)
        {
            if (selected[candidate])
            {
                continue;
            }

            if ((CAN_AI_MAX_PROFILES == best) ||
                (detector->profiles[candidate].observed_frames >
                 detector->profiles[best].observed_frames) ||
                ((detector->profiles[candidate].observed_frames ==
                  detector->profiles[best].observed_frames) &&
                 (detector->profiles[candidate].id < detector->profiles[best].id)))
            {
                best = candidate;
            }
        }

        if (CAN_AI_MAX_PROFILES == best)
        {
            break;
        }

        selected[best] = true;
        can_ai_profile_to_view(&detector->profiles[best], &profiles[output_count]);
    }

    return output_count;
}
