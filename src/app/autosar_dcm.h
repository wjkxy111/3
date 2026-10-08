#ifndef AUTOSAR_DCM_H
#define AUTOSAR_DCM_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_boot.h"

#define AUTOSAR_DCM_MAX_REQUEST       (128U)
#define AUTOSAR_DCM_MAX_RESPONSE      (128U)

typedef enum e_autosar_dcm_session
{
    AUTOSAR_DCM_SESSION_DEFAULT = 0x01U,
    AUTOSAR_DCM_SESSION_PROGRAMMING = 0x02U,
    AUTOSAR_DCM_SESSION_EXTENDED = 0x03U
} autosar_dcm_session_t;

typedef struct st_autosar_dcm
{
    autosar_dcm_session_t session;
    uint32_t request_count;
    uint32_t response_count;
    bool reset_pending;
    autosar_boot_validation_t last_boot_validation;
} autosar_dcm_t;

void autosar_dcm_init(autosar_dcm_t * dcm);
bool autosar_dcm_process(autosar_dcm_t * dcm,
                         uint8_t const * request,
                         uint16_t request_length,
                         uint8_t * response,
                         uint16_t response_capacity,
                         uint16_t * response_length);
char const * autosar_dcm_session_name(autosar_dcm_session_t session);

#endif /* AUTOSAR_DCM_H */
