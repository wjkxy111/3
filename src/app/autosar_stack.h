#ifndef AUTOSAR_STACK_H
#define AUTOSAR_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_dcm.h"

typedef struct st_autosar_stack_status
{
    bool initialized;
    uint32_t rx_requests;
    uint32_t tx_responses;
    autosar_dcm_session_t session;
    autosar_boot_validation_t boot_validation;
} autosar_stack_status_t;

void autosar_stack_init(void);
void autosar_stack_main_function(uint32_t now_ms);
bool autosar_stack_on_can_frame(uint32_t can_id,
                                uint8_t const * data,
                                uint8_t length,
                                uint32_t now_ms);
void autosar_stack_get_status(autosar_stack_status_t * status);

#endif /* AUTOSAR_STACK_H */
