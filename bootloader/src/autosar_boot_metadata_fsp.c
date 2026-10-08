#include "../include/autosar_boot_metadata_fsp.h"

#include <stddef.h>
#include <string.h>

#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)

static bool autosar_boot_metadata_range_ok(autosar_boot_metadata_fsp_t const * metadata,
                                           uint32_t index)
{
    return (NULL != metadata) && (index < AUTOSAR_UPDATE_METADATA_SLOTS) &&
           (metadata->block_size == sizeof(autosar_update_metadata_t)) &&
           (metadata->write_size != 0U);
}

bool autosar_boot_metadata_fsp_read(uint32_t index,
                                    autosar_update_metadata_t * record,
                                    void * context)
{
    autosar_boot_metadata_fsp_t const * metadata =
        (autosar_boot_metadata_fsp_t const *) context;
    if (!autosar_boot_metadata_range_ok(metadata, index) || (NULL == record))
    {
        return false;
    }
    memcpy(record, (void const *) (uintptr_t) (metadata->base_address +
                                               (index * metadata->block_size)),
           sizeof(*record));
    return true;
}

bool autosar_boot_metadata_fsp_write(uint32_t index,
                                     autosar_update_metadata_t const * record,
                                     void * context)
{
    autosar_boot_metadata_fsp_t * metadata =
        (autosar_boot_metadata_fsp_t *) context;
    autosar_update_metadata_t staging;
    uint32_t const address = (NULL != metadata) ?
                             (metadata->base_address + (index * metadata->block_size)) : 0U;
    uint32_t commit_marker = AUTOSAR_UPDATE_METADATA_COMMITTED;
    if (!autosar_boot_metadata_range_ok(metadata, index) || !metadata->opened ||
        (NULL == record) || ((sizeof(*record) % metadata->write_size) != 0U))
    {
        return false;
    }
    staging = *record;
    staging.commit_marker = 0xFFFFFFFFUL;
    if (FSP_SUCCESS != R_FLASH_HP_Erase(metadata->flash_ctrl, address, 1U))
    {
        return false;
    }
    if (FSP_SUCCESS != R_FLASH_HP_Write(metadata->flash_ctrl,
                                        (uint32_t) (uintptr_t) &staging,
                                        address, (uint32_t) sizeof(staging)))
    {
        return false;
    }
    /* Commit marker is the final programming operation. A reset before this
     * write leaves the record invalid and the other journal slot wins. */
    return FSP_SUCCESS == R_FLASH_HP_Write(metadata->flash_ctrl,
                                            (uint32_t) (uintptr_t) &commit_marker,
                                            address + (uint32_t) offsetof(autosar_update_metadata_t,
                                                                          commit_marker),
                                            (uint32_t) sizeof(commit_marker));
}

#else

bool autosar_boot_metadata_fsp_read(uint32_t index,
                                    autosar_update_metadata_t * record,
                                    void * context)
{
    (void) index; (void) record; (void) context;
    return false;
}

bool autosar_boot_metadata_fsp_write(uint32_t index,
                                     autosar_update_metadata_t const * record,
                                     void * context)
{
    (void) index; (void) record; (void) context;
    return false;
}

#endif
