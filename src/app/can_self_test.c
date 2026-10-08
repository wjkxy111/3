#include "can_self_test.h"

#include <stddef.h>
#include <string.h>

#define SELF_TEST_NORMAL_TIME_US  (5000000UL)
#define SELF_TEST_ATTACK_TIME_US  (1400000UL)

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t) (now - deadline) >= 0);
}

static void fill_normal_payload(can_self_test_frame_t * frame, uint32_t sequence)
{
    frame->data[0] = (uint8_t) sequence;
    frame->data[1] = (uint8_t) (sequence >> 8);
    frame->data[2] = (uint8_t) (20U + (sequence % 7U));
    frame->data[3] = (uint8_t) (80U + (sequence % 5U));
    frame->data[4] = 0x34U;
    frame->data[5] = 0x12U;
    frame->data[6] = 0x5AU;
    frame->data[7] = 0xA5U;
}

void can_self_test_init(can_self_test_t * self_test, uint32_t now_us)
{
    if (NULL == self_test)
    {
        return;
    }
    memset(self_test, 0, sizeof(*self_test));
    self_test->next_100_us = now_us;
    self_test->next_180_us = now_us;
    self_test->next_240_us = now_us;
    self_test->phase_start_us = now_us;
    self_test->phase = CAN_SELF_TEST_NORMAL;
}

void can_self_test_set_enabled(can_self_test_t * self_test, bool enabled, uint32_t now_us)
{
    if (NULL == self_test)
    {
        return;
    }
    can_self_test_init(self_test, now_us);
    self_test->enabled = enabled;
}

bool can_self_test_enabled(can_self_test_t const * self_test)
{
    return (NULL != self_test) && self_test->enabled;
}

can_self_test_phase_t can_self_test_phase(can_self_test_t const * self_test)
{
    return (NULL != self_test) ? self_test->phase : CAN_SELF_TEST_NORMAL;
}

char const * can_self_test_phase_name(can_self_test_phase_t phase)
{
    static char const * const names[] = {"NORMAL", "UNKNOWN_ID", "DLC_CHANGE", "PAYLOAD_JUMP", "FLOOD"};
    return (phase <= CAN_SELF_TEST_FLOOD) ? names[phase] : "INVALID";
}

static void advance_phase(can_self_test_t * self_test, uint32_t now_us)
{
    uint32_t const elapsed = now_us - self_test->phase_start_us;
    uint32_t const duration = (CAN_SELF_TEST_NORMAL == self_test->phase) ?
                              SELF_TEST_NORMAL_TIME_US : SELF_TEST_ATTACK_TIME_US;
    if (elapsed < duration)
    {
        return;
    }

    self_test->phase = (can_self_test_phase_t) (((uint32_t) self_test->phase + 1U) %
                                                ((uint32_t) CAN_SELF_TEST_FLOOD + 1U));
    self_test->phase_start_us = now_us;
    self_test->next_100_us = now_us;
    self_test->next_180_us = now_us;
    self_test->next_240_us = now_us;
}

bool can_self_test_next(can_self_test_t * self_test, uint32_t now_us, can_self_test_frame_t * frame)
{
    if ((NULL == self_test) || (NULL == frame) || (!self_test->enabled))
    {
        return false;
    }

    advance_phase(self_test, now_us);
    memset(frame, 0, sizeof(*frame));
    frame->data_length_code = 8U;

    bool const due_100 = time_reached(now_us, self_test->next_100_us);
    bool const due_180 = time_reached(now_us, self_test->next_180_us);
    bool const due_240 = time_reached(now_us, self_test->next_240_us);
    if ((!due_100) && (!due_180) && (!due_240))
    {
        return false;
    }

    /* Pick the oldest scheduled stream so catch-up frames remain monotonic. */
    uint8_t stream = 0U;
    uint32_t scheduled_us = self_test->next_100_us;
    bool selected = due_100;
    if (due_180 && ((!selected) || ((int32_t) (self_test->next_180_us - scheduled_us) < 0)))
    {
        stream = 1U;
        scheduled_us = self_test->next_180_us;
        selected = true;
    }
    if (due_240 && ((!selected) || ((int32_t) (self_test->next_240_us - scheduled_us) < 0)))
    {
        stream = 2U;
        scheduled_us = self_test->next_240_us;
    }
    frame->timestamp_us = scheduled_us;

    if (0U == stream)
    {
        bool const unknown = (CAN_SELF_TEST_UNKNOWN_ID == self_test->phase) &&
                             ((self_test->sequence % 17U) == 0U);
        frame->id = unknown ? 0x666U : 0x100U;
        fill_normal_payload(frame, self_test->sequence);
        self_test->next_100_us += (CAN_SELF_TEST_FLOOD == self_test->phase) ? 800U : 10000U;
        if (CAN_SELF_TEST_DLC_CHANGE == self_test->phase)
        {
            frame->data_length_code = 4U;
        }
        if (CAN_SELF_TEST_PAYLOAD_JUMP == self_test->phase)
        {
            frame->data[2] = 0xF2U;
            frame->data[3] = 0x04U;
            frame->data[6] ^= 0xFFU;
        }
    }
    else if (1U == stream)
    {
        frame->id = 0x180U;
        fill_normal_payload(frame, self_test->sequence / 2U);
        self_test->next_180_us += 20000U;
    }
    else
    {
        frame->id = 0x240U;
        fill_normal_payload(frame, self_test->sequence / 10U);
        self_test->next_240_us += 100000U;
    }

    self_test->sequence++;
    return true;
}
