#ifndef APP_CAN_EVENT_LOG_H
#define APP_CAN_EVENT_LOG_H

#include <stddef.h>
#include <stdint.h>

#define CAN_EVENT_LOG_CAPACITY (16U)

typedef struct
{
    uint64_t timestamp_us;
    uint32_t can_id;
    uint32_t reason_mask;
    uint16_t score;
    uint8_t data_length;
    uint8_t flags;
    uint8_t sequence;
} can_event_record_t;

typedef struct
{
    can_event_record_t records[CAN_EVENT_LOG_CAPACITY];
    uint32_t total_count;
    uint8_t head;
    uint8_t count;
} can_event_log_t;

void can_event_log_init(can_event_log_t * log);
void can_event_log_push(can_event_log_t * log, can_event_record_t const * record);
size_t can_event_log_copy_recent(can_event_log_t const * log, can_event_record_t * output, size_t capacity);

#endif /* APP_CAN_EVENT_LOG_H */
