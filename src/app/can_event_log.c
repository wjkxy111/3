#include "can_event_log.h"

#include <string.h>

void can_event_log_init(can_event_log_t * log)
{
    if (NULL != log)
    {
        memset(log, 0, sizeof(*log));
    }
}

void can_event_log_push(can_event_log_t * log, can_event_record_t const * record)
{
    if ((NULL == log) || (NULL == record))
    {
        return;
    }

    can_event_record_t value = *record;
    value.sequence = (uint8_t) log->total_count;
    log->records[log->head] = value;
    log->head = (uint8_t) ((log->head + 1U) % CAN_EVENT_LOG_CAPACITY);
    if (log->count < CAN_EVENT_LOG_CAPACITY)
    {
        log->count++;
    }
    log->total_count++;
}

size_t can_event_log_copy_recent(can_event_log_t const * log, can_event_record_t * output, size_t capacity)
{
    if ((NULL == log) || (NULL == output))
    {
        return 0U;
    }

    size_t count = log->count;
    if (count > capacity)
    {
        count = capacity;
    }

    for (size_t i = 0U; i < count; i++)
    {
        uint8_t const index = (uint8_t) ((log->head + CAN_EVENT_LOG_CAPACITY - 1U - i) %
                                         CAN_EVENT_LOG_CAPACITY);
        output[i] = log->records[index];
    }
    return count;
}
