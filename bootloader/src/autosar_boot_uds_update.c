#include "../include/autosar_boot_uds_update.h"

#include <stddef.h>
#include <string.h>

static uint32_t uds_be32(uint8_t const * data)
{
    return ((uint32_t) data[0] << 24U) | ((uint32_t) data[1] << 16U) |
           ((uint32_t) data[2] << 8U) | (uint32_t) data[3];
}

static bool uds_positive(uint8_t service, uint8_t const * payload, uint16_t payload_length,
                         uint8_t * response, uint16_t capacity, uint16_t * length)
{
    if ((NULL == response) || (NULL == length) ||
        ((uint32_t) payload_length + 1U > capacity))
    {
        return false;
    }
    response[0] = (uint8_t) (service + 0x40U);
    if ((payload_length > 0U) && (NULL != payload))
    {
        memcpy(&response[1], payload, payload_length);
    }
    *length = (uint16_t) (payload_length + 1U);
    return true;
}

static bool uds_negative(uint8_t service, uint8_t nrc,
                         uint8_t * response, uint16_t capacity, uint16_t * length)
{
    if ((NULL == response) || (NULL == length) || (capacity < 3U))
    {
        return false;
    }
    response[0] = 0x7FU;
    response[1] = service;
    response[2] = nrc;
    *length = 3U;
    return true;
}

void autosar_boot_uds_update_init(autosar_boot_uds_update_t * uds,
                                  autosar_update_t * update)
{
    if (NULL == uds)
    {
        return;
    }
    memset(uds, 0, sizeof(*uds));
    uds->update = update;
    uds->next_block_counter = 1U;
}

bool autosar_boot_uds_update_process(autosar_boot_uds_update_t * uds,
                                     uint8_t const * request,
                                     uint16_t request_length,
                                     uint8_t * response,
                                     uint16_t response_capacity,
                                     uint16_t * response_length)
{
    uint8_t const service = (request_length > 0U) ? request[0] : 0U;
    if ((NULL == uds) || (NULL == uds->update) || (NULL == request) ||
        (request_length == 0U) || (NULL == response) || (NULL == response_length))
    {
        return false;
    }
    *response_length = 0U;
    switch (service)
    {
        case AUTOSAR_BOOT_UDS_SERVICE_REQUEST_DOWNLOAD:
        {
            /* Project-defined secure RequestDownload. It keeps the fixed slot
             * address implicit and carries the complete 256-byte-header
             * security fields in one ISO-TP request (127 bytes total). */
            uint8_t response_payload[2] = {0U, AUTOSAR_BOOT_UDS_MAX_TRANSFER_DATA};
            if ((request_length != 127U) || (request[1] != 0U) || (request[2] != 0U))
            {
                return uds_negative(service, 0x13U, response, response_capacity,
                                    response_length);
            }
            memset(&uds->header, 0, sizeof(uds->header));
            uds->header.magic = AUTOSAR_SECURE_IMAGE_MAGIC;
            uds->header.header_size = AUTOSAR_SECURE_IMAGE_HEADER_SIZE;
            uds->header.format_version = AUTOSAR_SECURE_IMAGE_FORMAT_VERSION;
            uds->header.image_size = uds_be32(&request[3]);
            uds->header.image_version = uds_be32(&request[7]);
            uds->header.security_counter = uds_be32(&request[11]);
            uds->header.vector_sp = uds_be32(&request[15]);
            uds->header.vector_reset = uds_be32(&request[19]);
            uds->header.flags = uds_be32(&request[23]);
            uds->header.key_id = uds_be32(&request[27]);
            memcpy(uds->header.image_hash, &request[31],
                   AUTOSAR_SECURE_IMAGE_HASH_SIZE);
            memcpy(uds->header.signature, &request[63],
                   AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE);
            if (AUTOSAR_UPDATE_OK != autosar_update_begin(
                    uds->update, uds->header.image_size, uds->header.image_version,
                    uds->header.security_counter))
            {
                return uds_negative(service, 0x72U, response, response_capacity,
                                    response_length);
            }
            uds->active = true;
            uds->next_block_counter = 1U;
            return uds_positive(service, response_payload, sizeof(response_payload),
                                response, response_capacity, response_length);
        }

        case AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_DATA:
            if (!uds->active || (request_length < 3U) ||
                (request[1] != uds->next_block_counter) ||
                (request_length - 2U > AUTOSAR_BOOT_UDS_MAX_TRANSFER_DATA))
            {
                return uds_negative(service, 0x73U, response, response_capacity,
                                    response_length);
            }
            if (AUTOSAR_UPDATE_OK != autosar_update_write(uds->update,
                                                           uds->update->received,
                                                           &request[2],
                                                           (uint32_t) request_length - 2U))
            {
                return uds_negative(service, 0x72U, response, response_capacity,
                                    response_length);
            }
            uds->next_block_counter++;
            return uds_positive(service, &request[1], 1U, response,
                                response_capacity, response_length);

        case AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_EXIT:
            if (!uds->active || (request_length != 1U))
            {
                return uds_negative(service, 0x24U, response, response_capacity,
                                    response_length);
            }
            if (AUTOSAR_UPDATE_OK != autosar_update_finish(uds->update, &uds->header))
            {
                uds->active = false;
                return uds_negative(service, 0x72U, response, response_capacity,
                                    response_length);
            }
            uds->active = false;
            return uds_positive(service, NULL, 0U, response,
                                response_capacity, response_length);

        default:
            return uds_negative(service, 0x11U, response, response_capacity,
                                response_length);
    }
}
