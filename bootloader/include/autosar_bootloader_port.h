#ifndef AUTOSAR_BOOTLOADER_PORT_H
#define AUTOSAR_BOOTLOADER_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "../../src/app/autosar_boot.h"
#include "autosar_secure_image.h"

typedef struct st_autosar_bootloader_port
{
    autosar_boot_read_fn read_flash;
    void * flash_context;
    bool (* update_requested)(void * context);
    bool (* jump_to_application)(uint32_t vector_address, void * context);
    void * context;
    autosar_secure_hash_fn hash_image;
    autosar_secure_verify_fn verify_signature;
    void * secure_context;
} autosar_bootloader_port_t;

typedef enum e_autosar_bootloader_decision
{
    AUTOSAR_BOOTLOADER_JUMP_APPLICATION = 0,
    AUTOSAR_BOOTLOADER_ENTER_UPDATE,
    AUTOSAR_BOOTLOADER_RECOVERY_REQUIRED
} autosar_bootloader_decision_t;

autosar_bootloader_decision_t autosar_bootloader_select(autosar_bootloader_port_t const * port);
bool autosar_bootloader_run(autosar_bootloader_port_t const * port);
autosar_bootloader_decision_t autosar_bootloader_select_secure(
    autosar_bootloader_port_t const * port,
    uint32_t minimum_security_counter);
bool autosar_bootloader_run_secure(autosar_bootloader_port_t const * port,
                                   uint32_t minimum_security_counter);
autosar_bootloader_decision_t autosar_bootloader_entry(
    autosar_bootloader_port_t const * port,
    uint32_t minimum_security_counter);

#endif /* AUTOSAR_BOOTLOADER_PORT_H */
