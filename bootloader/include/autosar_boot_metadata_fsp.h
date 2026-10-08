#ifndef AUTOSAR_BOOT_METADATA_FSP_H
#define AUTOSAR_BOOT_METADATA_FSP_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_boot_update.h"

#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)
#include "r_flash_api.h"
#endif

typedef struct st_autosar_boot_metadata_fsp
{
#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)
    flash_ctrl_t * flash_ctrl;
#else
    void * flash_ctrl;
#endif
    uint32_t base_address;
    uint32_t block_size;
    uint32_t write_size;
    bool opened;
} autosar_boot_metadata_fsp_t;

bool autosar_boot_metadata_fsp_read(uint32_t index,
                                    autosar_update_metadata_t * record,
                                    void * context);
bool autosar_boot_metadata_fsp_write(uint32_t index,
                                     autosar_update_metadata_t const * record,
                                     void * context);

#endif /* AUTOSAR_BOOT_METADATA_FSP_H */
