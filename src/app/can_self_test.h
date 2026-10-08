#ifndef APP_CAN_SELF_TEST_H
#define APP_CAN_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t id;
    uint32_t timestamp_us;
    uint8_t data[64];
    uint8_t data_length_code;
    bool extended_id;
    bool fd_frame;
    bool bitrate_switch;
} can_self_test_frame_t;

typedef enum
{
    CAN_SELF_TEST_NORMAL = 0,
    CAN_SELF_TEST_UNKNOWN_ID,
    CAN_SELF_TEST_DLC_CHANGE,
    CAN_SELF_TEST_PAYLOAD_JUMP,
    CAN_SELF_TEST_FLOOD
} can_self_test_phase_t;

typedef struct
{
    uint32_t next_100_us;
    uint32_t next_180_us;
    uint32_t next_240_us;
    uint32_t phase_start_us;
    uint32_t sequence;
    can_self_test_phase_t phase;
    bool enabled;
} can_self_test_t;

void can_self_test_init(can_self_test_t * self_test, uint32_t now_us);
void can_self_test_set_enabled(can_self_test_t * self_test, bool enabled, uint32_t now_us);
bool can_self_test_enabled(can_self_test_t const * self_test);
can_self_test_phase_t can_self_test_phase(can_self_test_t const * self_test);
char const * can_self_test_phase_name(can_self_test_phase_t phase);
bool can_self_test_next(can_self_test_t * self_test, uint32_t now_us, can_self_test_frame_t * frame);

#endif /* APP_CAN_SELF_TEST_H */
