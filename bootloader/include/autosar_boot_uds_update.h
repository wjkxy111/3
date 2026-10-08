#ifndef AUTOSAR_BOOT_UDS_UPDATE_H
#define AUTOSAR_BOOT_UDS_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_boot_update.h"

#define AUTOSAR_BOOT_UDS_SERVICE_REQUEST_DOWNLOAD (0x34U)
#define AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_DATA    (0x36U)
#define AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_EXIT    (0x37U)
#define AUTOSAR_BOOT_UDS_MAX_TRANSFER_DATA        (120U)

typedef struct st_autosar_boot_uds_update
{
    autosar_update_t * update;
    autosar_secure_image_header_t header;
    uint8_t next_block_counter;
    bool active;
} autosar_boot_uds_update_t;

void autosar_boot_uds_update_init(autosar_boot_uds_update_t * uds,
                                  autosar_update_t * update);
bool autosar_boot_uds_update_process(autosar_boot_uds_update_t * uds,
                                     uint8_t const * request,
                                     uint16_t request_length,
                                     uint8_t * response,
                                     uint16_t response_capacity,
                                     uint16_t * response_length);

#endif /* AUTOSAR_BOOT_UDS_UPDATE_H */
