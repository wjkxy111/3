#ifndef AUTOSAR_BOOT_RSIP_H
#define AUTOSAR_BOOT_RSIP_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_secure_image.h"

#if defined(AUTOSAR_BOOT_USE_RSIP_E51A)
#include "r_rsip_protected_api.h"
#endif

typedef struct st_autosar_boot_rsip
{
#if defined(AUTOSAR_BOOT_USE_RSIP_E51A)
    rsip_ctrl_t * rsip_ctrl;
    rsip_cfg_t const * rsip_cfg;
    rsip_wrapped_key_t const * production_public_key;
#else
    void * rsip_ctrl;
    void const * rsip_cfg;
    void const * production_public_key;
#endif
    bool opened;
} autosar_boot_rsip_t;

bool autosar_boot_rsip_open(autosar_boot_rsip_t * rsip);
bool autosar_boot_rsip_close(autosar_boot_rsip_t * rsip);
bool autosar_boot_rsip_hash(uint32_t address, uint32_t length,
                            uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                            void * context);
bool autosar_boot_rsip_verify(uint8_t const hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                              uint8_t const signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE],
                              uint32_t key_id, void * context);

#endif /* AUTOSAR_BOOT_RSIP_H */
