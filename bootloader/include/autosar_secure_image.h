#ifndef AUTOSAR_SECURE_IMAGE_H
#define AUTOSAR_SECURE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUTOSAR_SECURE_IMAGE_MAGIC              (0x53454349UL) /* "SECI" */
#define AUTOSAR_SECURE_IMAGE_FORMAT_VERSION    (2U)
#define AUTOSAR_SECURE_IMAGE_HEADER_SIZE       (256U)
#define AUTOSAR_SECURE_IMAGE_HASH_SIZE         (32U)
#define AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE    (64U) /* raw ECDSA r || s */
#define AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION (1U)

#define AUTOSAR_SECURE_SLOT_START              (0x02010000UL)
#define AUTOSAR_SECURE_SLOT_SIZE               (0x001E8000UL)
#define AUTOSAR_SECURE_APP_IMAGE_START        (AUTOSAR_SECURE_SLOT_START + AUTOSAR_SECURE_IMAGE_HEADER_SIZE)

typedef struct st_autosar_secure_image_header
{
    uint32_t magic;
    uint16_t header_size;
    uint16_t format_version;
    uint32_t image_size;
    uint32_t image_version;
    uint32_t security_counter;
    uint32_t vector_sp;
    uint32_t vector_reset;
    uint32_t flags;
    uint32_t key_id;
    uint8_t  image_hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
    uint8_t  signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE];
    uint32_t reserved[31];
} autosar_secure_image_header_t;

typedef bool (* autosar_secure_hash_fn)(uint32_t address,
                                        uint32_t length,
                                        uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                                        void * context);
typedef bool (* autosar_secure_verify_fn)(uint8_t const hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                                          uint8_t const signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE],
                                          uint32_t key_id,
                                          void * context);

typedef enum e_autosar_secure_validation
{
    AUTOSAR_SECURE_VALID = 0,
    AUTOSAR_SECURE_ERR_ARGUMENT,
    AUTOSAR_SECURE_ERR_HEADER,
    AUTOSAR_SECURE_ERR_RANGE,
    AUTOSAR_SECURE_ERR_VECTOR,
    AUTOSAR_SECURE_ERR_ROLLBACK,
    AUTOSAR_SECURE_ERR_HASH,
    AUTOSAR_SECURE_ERR_SIGNATURE
} autosar_secure_validation_t;

void autosar_sha256(uint8_t const * data, size_t length,
                    uint8_t digest[AUTOSAR_SECURE_IMAGE_HASH_SIZE]);
void autosar_secure_signing_digest(autosar_secure_image_header_t const * header,
                                   uint8_t digest[AUTOSAR_SECURE_IMAGE_HASH_SIZE]);
bool autosar_secure_header_valid(autosar_secure_image_header_t const * header,
                                 uint32_t slot_address,
                                 uint32_t slot_size);
autosar_secure_validation_t autosar_secure_validate(
    autosar_secure_image_header_t const * header,
    uint32_t slot_address,
    uint32_t slot_size,
    uint32_t minimum_security_counter,
    autosar_secure_hash_fn hash_fn,
    autosar_secure_verify_fn verify_fn,
    void * context);
char const * autosar_secure_validation_name(autosar_secure_validation_t status);

#endif /* AUTOSAR_SECURE_IMAGE_H */
