#include "autosar_dcm.h"

#include <string.h>

static bool autosar_dcm_put(uint8_t * response, uint16_t capacity,
                            uint16_t * length, uint8_t value)
{
    if ((NULL == response) || (NULL == length) || (*length >= capacity))
    {
        return false;
    }
    response[*length] = value;
    (*length)++;
    return true;
}

static bool autosar_dcm_negative(uint8_t service, uint8_t nrc,
                                 uint8_t * response, uint16_t capacity,
                                 uint16_t * length)
{
    *length = 0U;
    return autosar_dcm_put(response, capacity, length, 0x7FU) &&
           autosar_dcm_put(response, capacity, length, service) &&
           autosar_dcm_put(response, capacity, length, nrc);
}

void autosar_dcm_init(autosar_dcm_t * dcm)
{
    if (NULL == dcm)
    {
        return;
    }
    memset(dcm, 0, sizeof(*dcm));
    dcm->session = AUTOSAR_DCM_SESSION_DEFAULT;
    dcm->last_boot_validation = AUTOSAR_BOOT_ERR_READER;
}

char const * autosar_dcm_session_name(autosar_dcm_session_t session)
{
    switch (session)
    {
        case AUTOSAR_DCM_SESSION_DEFAULT:    return "DEFAULT";
        case AUTOSAR_DCM_SESSION_PROGRAMMING: return "PROGRAMMING";
        case AUTOSAR_DCM_SESSION_EXTENDED:   return "EXTENDED";
        default:                             return "UNKNOWN";
    }
}

bool autosar_dcm_process(autosar_dcm_t * dcm,
                         uint8_t const * request,
                         uint16_t request_length,
                         uint8_t * response,
                         uint16_t response_capacity,
                         uint16_t * response_length)
{
    uint8_t const service = (request_length > 0U) ? request[0] : 0U;
    uint16_t length = 0U;

    if ((NULL == dcm) || (NULL == request) || (request_length == 0U) ||
        (NULL == response) || (NULL == response_length) || (response_capacity == 0U))
    {
        return false;
    }
    dcm->request_count++;
    *response_length = 0U;

    switch (service)
    {
        case 0x10U: /* DiagnosticSessionControl */
            if ((request_length != 2U) ||
                ((request[1] != AUTOSAR_DCM_SESSION_DEFAULT) &&
                 (request[1] != AUTOSAR_DCM_SESSION_PROGRAMMING) &&
                 (request[1] != AUTOSAR_DCM_SESSION_EXTENDED)))
            {
                return autosar_dcm_negative(service, 0x12U, response, response_capacity,
                                            response_length);
            }
            dcm->session = (autosar_dcm_session_t) request[1];
            (void) autosar_dcm_put(response, response_capacity, &length, 0x50U);
            (void) autosar_dcm_put(response, response_capacity, &length, request[1]);
            (void) autosar_dcm_put(response, response_capacity, &length, 0x00U);
            (void) autosar_dcm_put(response, response_capacity, &length, 0x32U);
            (void) autosar_dcm_put(response, response_capacity, &length, 0x01U);
            (void) autosar_dcm_put(response, response_capacity, &length, 0xF4U);
            *response_length = length;
            break;

        case 0x11U: /* ECUReset: acknowledge, reset is supervised by stack */
            if ((request_length != 2U) || (request[1] != 0x01U))
            {
                return autosar_dcm_negative(service, 0x12U, response, response_capacity,
                                            response_length);
            }
            dcm->reset_pending = true;
            (void) autosar_dcm_put(response, response_capacity, &length, 0x51U);
            (void) autosar_dcm_put(response, response_capacity, &length, request[1]);
            *response_length = length;
            break;

        case 0x22U: /* ReadDataByIdentifier */
            if (request_length != 3U)
            {
                return autosar_dcm_negative(service, 0x13U, response, response_capacity,
                                            response_length);
            }
            (void) autosar_dcm_put(response, response_capacity, &length, 0x62U);
            (void) autosar_dcm_put(response, response_capacity, &length, request[1]);
            (void) autosar_dcm_put(response, response_capacity, &length, request[2]);
            if ((request[1] == 0xF1U) && (request[2] == 0x90U))
            {
                static uint8_t const version[] = "RA8D1-CAN-AUTOSAR-1.0";
                for (size_t i = 0U; i < (sizeof(version) - 1U); i++)
                {
                    (void) autosar_dcm_put(response, response_capacity, &length, version[i]);
                }
            }
            else if ((request[1] == 0xF1U) && (request[2] == 0x86U))
            {
                (void) autosar_dcm_put(response, response_capacity, &length,
                                        (uint8_t) dcm->session);
            }
            else
            {
                return autosar_dcm_negative(service, 0x31U, response, response_capacity,
                                            response_length);
            }
            *response_length = length;
            break;

        case 0x19U: /* ReadDTCInformation: one logical zero-DTC response */
            if ((request_length != 2U) || (request[1] != 0x02U))
            {
                return autosar_dcm_negative(service, 0x12U, response, response_capacity,
                                            response_length);
            }
            (void) autosar_dcm_put(response, response_capacity, &length, 0x59U);
            (void) autosar_dcm_put(response, response_capacity, &length, request[1]);
            (void) autosar_dcm_put(response, response_capacity, &length, 0x00U);
            *response_length = length;
            break;

        case 0x31U: /* RoutineControl: validation is intentionally not claimed valid
                     * until a bootloader supplies a flash reader. */
            if ((request_length != 4U) || (request[1] != 0x01U) ||
                (request[2] != 0x02U) || (request[3] != 0x01U))
            {
                return autosar_dcm_negative(service, 0x31U, response, response_capacity,
                                            response_length);
            }
            (void) autosar_dcm_put(response, response_capacity, &length, 0x71U);
            (void) autosar_dcm_put(response, response_capacity, &length, request[1]);
            (void) autosar_dcm_put(response, response_capacity, &length, request[2]);
            (void) autosar_dcm_put(response, response_capacity, &length, request[3]);
            (void) autosar_dcm_put(response, response_capacity, &length,
                                   (uint8_t) dcm->last_boot_validation);
            *response_length = length;
            break;

        default:
            return autosar_dcm_negative(service, 0x11U, response, response_capacity,
                                        response_length);
    }

    dcm->response_count++;
    return true;
}
