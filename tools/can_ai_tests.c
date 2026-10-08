#include "../src/can_ai/can_anomaly.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_invalid_config(void)
{
    const float values[] = {NAN, INFINITY, -INFINITY};
    can_ai_config_t defaults;
    can_ai_default_config(&defaults);
    for (unsigned int i = 0U; i < 3U; ++i)
    {
        can_ai_config_t config = defaults;
        config.period_sigma = values[i];
        config.period_jitter_floor_ratio = values[i];
        config.period_fast_ratio = values[i];
        config.period_slow_ratio = values[i];
        config.payload_sigma = values[i];
        config.payload_sigma_floor = values[i];
        config.flood_multiplier = values[i];
        can_ai_detector_t detector;
        can_ai_init(&detector, &config);
        assert(detector.config.period_sigma == defaults.period_sigma);
        assert(detector.config.period_jitter_floor_ratio == defaults.period_jitter_floor_ratio);
        assert(detector.config.period_fast_ratio == defaults.period_fast_ratio);
        assert(detector.config.period_slow_ratio == defaults.period_slow_ratio);
        assert(detector.config.payload_sigma == defaults.payload_sigma);
        assert(detector.config.payload_sigma_floor == defaults.payload_sigma_floor);
        assert(detector.config.flood_multiplier == defaults.flood_multiplier);
    }
}

int main(void)
{
    test_invalid_config();
    can_ai_detector_t detector;
    can_ai_init(&detector, NULL);
    detector.config.period_sigma = 6.0F;
    detector.config.rate_window_us = 2000000U;
    detector.config.payload_min_jump = 20U;
    detector.total_frames = 100U;
    can_ai_init(&detector, &detector.config);
    assert(detector.config.period_sigma == 6.0F);
    assert(detector.config.rate_window_us == 2000000U);
    assert(detector.config.payload_min_jump == 20U);
    assert(detector.total_frames == 0U);
    assert(!can_ai_finish_learning(&detector));
    unsigned char data[8] = {0};
    can_ai_frame_t frame = {0x100U, CAN_AI_ID_STANDARD, 8U, data, 0U};
    can_ai_result_t result;
    for (unsigned int i = 0U; i < 30U; ++i)
    {
        frame.timestamp_us = (uint64_t)i * 10000U;
        assert(can_ai_process(&detector, &frame, &result) == CAN_AI_STATUS_OK);
    }
    assert(can_ai_finish_learning(&detector));
    frame.timestamp_us += 10000U;
    can_ai_process(&detector, &frame, &result);
    assert(result.reasons == CAN_AI_REASON_NONE);
    frame.timestamp_us += 100U;
    can_ai_process(&detector, &frame, &result);
    assert(result.reasons & CAN_AI_REASON_PERIOD_FAST);
    data[0] = 255U;
    frame.timestamp_us += 10000U;
    can_ai_process(&detector, &frame, &result);
    assert(result.reasons & CAN_AI_REASON_PAYLOAD_JUMP);
    frame.id = 0x333U;
    can_ai_process(&detector, &frame, &result);
    assert(result.reasons & CAN_AI_REASON_UNKNOWN_ID);
    frame.id = 0x800U;
    assert(can_ai_process(&detector, &frame, &result) == CAN_AI_STATUS_INVALID_ARGUMENT);
    puts("CAN invalid configuration, aliased reset, learning and anomaly checks passed");
    return 0;
}
