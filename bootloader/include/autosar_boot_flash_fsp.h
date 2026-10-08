#ifndef AUTOSAR_BOOT_FLASH_FSP_H
#define AUTOSAR_BOOT_FLASH_FSP_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_boot_update.h"

#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)
#include "r_flash_api.h"
#endif

typedef void (* autosar_boot_flash_critical_fn)(void * context);

typedef struct st_autosar_boot_flash_fsp
{
#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)
    flash_ctrl_t * flash_ctrl;
    flash_cfg_t const * flash_cfg;
    flash_arc_t anti_rollback_counter;
#else
    void * flash_ctrl;
    void const * flash_cfg;
#endif
    uint32_t slot_address;
    uint32_t slot_size;
    uint32_t erase_block_size;
    uint32_t write_granularity;
    autosar_boot_flash_critical_fn critical_enter;
    autosar_boot_flash_critical_fn critical_exit;
    void * critical_context;
    bool opened;
} autosar_boot_flash_fsp_t;

bool autosar_boot_flash_fsp_open(autosar_boot_flash_fsp_t * flash);
bool autosar_boot_flash_fsp_close(autosar_boot_flash_fsp_t * flash);
bool autosar_boot_flash_fsp_read(uint32_t address, uint8_t * data,
                                 uint32_t length, void * context);
bool autosar_boot_flash_fsp_erase(uint32_t address, uint32_t length,
                                  void * context);
bool autosar_boot_flash_fsp_write(uint32_t address, uint8_t const * data,
                                  uint32_t length, void * context);
bool autosar_boot_flash_fsp_blank_check(uint32_t address, uint32_t length,
                                        void * context);
bool autosar_boot_flash_fsp_hash(uint32_t address, uint32_t length,
                                 uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                                 void * context);
bool autosar_boot_flash_fsp_advance_counter(uint32_t target_security_counter,
                                            void * context);

#if defined(AUTOSAR_BOOT_USE_FSP_FLASH)
bool autosar_boot_flash_fsp_read_anti_rollback(autosar_boot_flash_fsp_t * flash,
                                                flash_arc_t counter,
                                                uint32_t * value);
bool autosar_boot_flash_fsp_advance_anti_rollback(autosar_boot_flash_fsp_t * flash,
                                                   flash_arc_t counter,
                                                   uint32_t target_value);
#endif

#endif /* AUTOSAR_BOOT_FLASH_FSP_H */
