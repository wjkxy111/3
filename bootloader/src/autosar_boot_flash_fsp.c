#include "../include/autosar_boot_flash_fsp.h"

#include <stddef.h>
#include <string.h>

#include "../include/autosar_secure_image.h"

#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)

#include "bsp_api.h"

#ifndef AUTOSAR_BOOT_FLASH_RAMFUNC
#define AUTOSAR_BOOT_FLASH_RAMFUNC
#endif

static bool autosar_boot_flash_range_ok(autosar_boot_flash_fsp_t const * flash,
                                        uint32_t address, uint32_t length)
{
    uint32_t end;
    uint32_t slot_end;
    if ((NULL == flash) || (length == 0U))
    {
        return false;
    }
    end = address + length;
    slot_end = flash->slot_address + flash->slot_size;
    return
           (address >= flash->slot_address) &&
           (end >= address) && (end <= slot_end);
}

bool autosar_boot_flash_fsp_open(autosar_boot_flash_fsp_t * flash)
{
    if ((NULL == flash) || (NULL == flash->flash_ctrl) || (NULL == flash->flash_cfg))
    {
        return false;
    }
    if (FSP_SUCCESS != R_FLASH_HP_Open(flash->flash_ctrl, flash->flash_cfg))
    {
        return false;
    }
    flash->opened = true;
    return true;
}

bool autosar_boot_flash_fsp_close(autosar_boot_flash_fsp_t * flash)
{
    if ((NULL == flash) || !flash->opened)
    {
        return false;
    }
    flash->opened = (FSP_SUCCESS != R_FLASH_HP_Close(flash->flash_ctrl));
    return !flash->opened;
}

bool autosar_boot_flash_fsp_read(uint32_t address, uint8_t * data,
                                 uint32_t length, void * context)
{
    autosar_boot_flash_fsp_t const * flash = (autosar_boot_flash_fsp_t const *) context;
    if ((NULL == flash) || (NULL == data) || !autosar_boot_flash_range_ok(flash, address, length))
    {
        return false;
    }
    memcpy(data, (void const *) (uintptr_t) address, length);
    return true;
}

AUTOSAR_BOOT_FLASH_RAMFUNC bool autosar_boot_flash_fsp_erase(uint32_t address,
                                                             uint32_t length,
                                                             void * context)
{
    autosar_boot_flash_fsp_t * flash = (autosar_boot_flash_fsp_t *) context;
    bool ok;
    if ((NULL == flash) || !flash->opened ||
        !autosar_boot_flash_range_ok(flash, address, length) ||
        (0U == flash->erase_block_size) ||
        ((address % flash->erase_block_size) != 0U) ||
        ((length % flash->erase_block_size) != 0U))
    {
        return false;
    }
    if (NULL != flash->critical_enter)
    {
        flash->critical_enter(flash->critical_context);
    }
    ok = (FSP_SUCCESS == R_FLASH_HP_Erase(flash->flash_ctrl, address,
                                           length / flash->erase_block_size));
    if (NULL != flash->critical_exit)
    {
        flash->critical_exit(flash->critical_context);
    }
    return ok;
}

AUTOSAR_BOOT_FLASH_RAMFUNC bool autosar_boot_flash_fsp_write(uint32_t address,
                                                             uint8_t const * data,
                                                             uint32_t length,
                                                             void * context)
{
    autosar_boot_flash_fsp_t * flash = (autosar_boot_flash_fsp_t *) context;
    bool ok;
    if ((NULL == flash) || !flash->opened || (NULL == data) ||
        !autosar_boot_flash_range_ok(flash, address, length) ||
        (0U == flash->write_granularity) ||
        ((address % flash->write_granularity) != 0U) ||
        ((length % flash->write_granularity) != 0U))
    {
        return false;
    }
    if (NULL != flash->critical_enter)
    {
        flash->critical_enter(flash->critical_context);
    }
    ok = (FSP_SUCCESS == R_FLASH_HP_Write(flash->flash_ctrl,
                                           (uint32_t) (uintptr_t) data,
                                           address, length));
    if (NULL != flash->critical_exit)
    {
        flash->critical_exit(flash->critical_context);
    }
    return ok;
}

bool autosar_boot_flash_fsp_blank_check(uint32_t address, uint32_t length,
                                        void * context)
{
    autosar_boot_flash_fsp_t * flash = (autosar_boot_flash_fsp_t *) context;
    flash_result_t result;
    if ((NULL == flash) || !flash->opened ||
        !autosar_boot_flash_range_ok(flash, address, length))
    {
        return false;
    }
    if (FSP_SUCCESS != R_FLASH_HP_BlankCheck(flash->flash_ctrl, address, length, &result))
    {
        return false;
    }
    return FLASH_RESULT_BLANK == result;
}

bool autosar_boot_flash_fsp_hash(uint32_t address, uint32_t length,
                                 uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                                 void * context)
{
    autosar_boot_flash_fsp_t const * flash = (autosar_boot_flash_fsp_t const *) context;
    if ((NULL == flash) || (NULL == hash) ||
        !autosar_boot_flash_range_ok(flash, address, length))
    {
        return false;
    }
    autosar_sha256((uint8_t const *) (uintptr_t) address, length, hash);
    return true;
}

bool autosar_boot_flash_fsp_advance_counter(uint32_t target_security_counter,
                                            void * context)
{
    autosar_boot_flash_fsp_t * flash = (autosar_boot_flash_fsp_t *) context;
    if (NULL == flash)
    {
        return false;
    }
    return autosar_boot_flash_fsp_advance_anti_rollback(flash,
                                                        flash->anti_rollback_counter,
                                                        target_security_counter);
}

bool autosar_boot_flash_fsp_read_anti_rollback(autosar_boot_flash_fsp_t * flash,
                                                flash_arc_t counter,
                                                uint32_t * value)
{
    if ((NULL == flash) || !flash->opened || (NULL == value))
    {
        return false;
    }
    return FSP_SUCCESS == R_FLASH_HP_AntiRollbackCounterRead(flash->flash_ctrl,
                                                               counter, value);
}

bool autosar_boot_flash_fsp_advance_anti_rollback(autosar_boot_flash_fsp_t * flash,
                                                   flash_arc_t counter,
                                                   uint32_t target_value)
{
    uint32_t current;
    if (!autosar_boot_flash_fsp_read_anti_rollback(flash, counter, &current) ||
        (target_value < current) || ((target_value - current) > 1024U))
    {
        return false;
    }
    while (current < target_value)
    {
        if (FSP_SUCCESS != R_FLASH_HP_AntiRollbackCounterIncrement(flash->flash_ctrl,
                                                                     counter))
        {
            (void) R_FLASH_HP_AntiRollbackCounterRefresh(flash->flash_ctrl, counter);
            return false;
        }
        current++;
    }
    return true;
}

#else

bool autosar_boot_flash_fsp_open(autosar_boot_flash_fsp_t * flash)
{
    (void) flash;
    return false;
}

bool autosar_boot_flash_fsp_close(autosar_boot_flash_fsp_t * flash)
{
    (void) flash;
    return false;
}

bool autosar_boot_flash_fsp_read(uint32_t address, uint8_t * data,
                                 uint32_t length, void * context)
{
    (void) address; (void) data; (void) length; (void) context;
    return false;
}

bool autosar_boot_flash_fsp_erase(uint32_t address, uint32_t length, void * context)
{
    (void) address; (void) length; (void) context;
    return false;
}

bool autosar_boot_flash_fsp_write(uint32_t address, uint8_t const * data,
                                  uint32_t length, void * context)
{
    (void) address; (void) data; (void) length; (void) context;
    return false;
}

bool autosar_boot_flash_fsp_blank_check(uint32_t address, uint32_t length,
                                        void * context)
{
    (void) address; (void) length; (void) context;
    return false;
}

bool autosar_boot_flash_fsp_hash(uint32_t address, uint32_t length,
                                 uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                                 void * context)
{
    (void) address; (void) length; (void) hash; (void) context;
    return false;
}

bool autosar_boot_flash_fsp_advance_counter(uint32_t target_security_counter,
                                            void * context)
{
    (void) target_security_counter; (void) context;
    return false;
}

#endif
