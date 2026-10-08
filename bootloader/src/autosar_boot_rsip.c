#include "../include/autosar_boot_rsip.h"

#include <stddef.h>

#if defined(AUTOSAR_BOOT_USE_RSIP_E51A)

bool autosar_boot_rsip_open(autosar_boot_rsip_t * rsip)
{
    if ((NULL == rsip) || (NULL == rsip->rsip_ctrl) || (NULL == rsip->rsip_cfg) ||
        (NULL == rsip->production_public_key))
    {
        return false;
    }
    if (FSP_SUCCESS != R_RSIP_Open(rsip->rsip_ctrl, rsip->rsip_cfg))
    {
        return false;
    }
    rsip->opened = true;
    return true;
}

bool autosar_boot_rsip_close(autosar_boot_rsip_t * rsip)
{
    if ((NULL == rsip) || !rsip->opened)
    {
        return false;
    }
    rsip->opened = (FSP_SUCCESS != R_RSIP_Close(rsip->rsip_ctrl));
    return !rsip->opened;
}

bool autosar_boot_rsip_hash(uint32_t address, uint32_t length,
                            uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                            void * context)
{
    autosar_boot_rsip_t * rsip = (autosar_boot_rsip_t *) context;
    if ((NULL == rsip) || !rsip->opened || (NULL == hash) || (0U == length))
    {
        return false;
    }
    return FSP_SUCCESS == R_RSIP_SHA_Compute(rsip->rsip_ctrl, RSIP_HASH_TYPE_SHA256,
                                              (uint8_t const *) (uintptr_t) address,
                                              length, hash);
}

bool autosar_boot_rsip_verify(uint8_t const hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                              uint8_t const signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE],
                              uint32_t key_id, void * context)
{
    autosar_boot_rsip_t * rsip = (autosar_boot_rsip_t *) context;
    if ((NULL == rsip) || !rsip->opened || (NULL == hash) || (NULL == signature) ||
        (key_id != AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION))
    {
        return false;
    }
    /* RA RSIP expects the ECDSA signature as the 32-byte r followed by the
     * 32-byte s value, exactly the image format used by the host signer. */
    return FSP_SUCCESS == R_RSIP_ECDSA_Verify(rsip->rsip_ctrl,
                                               rsip->production_public_key,
                                               hash, signature);
}

#else

bool autosar_boot_rsip_open(autosar_boot_rsip_t * rsip)
{
    (void) rsip;
    return false;
}

bool autosar_boot_rsip_close(autosar_boot_rsip_t * rsip)
{
    (void) rsip;
    return false;
}

bool autosar_boot_rsip_hash(uint32_t address, uint32_t length,
                            uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                            void * context)
{
    (void) address; (void) length; (void) hash; (void) context;
    return false;
}

bool autosar_boot_rsip_verify(uint8_t const hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                              uint8_t const signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE],
                              uint32_t key_id, void * context)
{
    (void) hash; (void) signature; (void) key_id; (void) context;
    return false;
}

#endif
