#ifndef AUTOSAR_BOOT_H
#define AUTOSAR_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "autosar_types.h"

/* RA8D1 main code flash is 2 MiB.  This first milestone reserves 64 KiB for
 * a bootloader and leaves the rest for the application image. */
#define AUTOSAR_BOOT_FLASH_START       (0x02000000UL)
#define AUTOSAR_BOOT_FLASH_SIZE        (0x001F8000UL)
#define AUTOSAR_BOOTLOADER_SIZE        (0x00010000UL)
#define AUTOSAR_APP_SLOT_START         (AUTOSAR_BOOT_FLASH_START + AUTOSAR_BOOTLOADER_SIZE)
#define AUTOSAR_APP_SLOT_SIZE          (0x001E8000UL)
#define AUTOSAR_BOOT_IMAGE_MAGIC       (0x4155544FUL) /* "AUTO" */
#define AUTOSAR_BOOT_IMAGE_VERSION     (1U)
#define AUTOSAR_BOOT_HEADER_SIZE       (64U)
#define AUTOSAR_APP_IMAGE_START        (AUTOSAR_APP_SLOT_START + AUTOSAR_BOOT_HEADER_SIZE)

typedef struct st_autosar_boot_image_header
{
    uint32_t magic;
    uint16_t header_size;
    uint16_t format_version;
    uint32_t image_size;
    uint32_t image_version;
    uint32_t image_crc32;
    uint32_t vector_sp;
    uint32_t vector_reset;
    uint32_t flags;
    uint32_t reserved[8];
} autosar_boot_image_header_t;

typedef bool (* autosar_boot_read_fn)(uint32_t address,
                                      uint8_t * data,
                                      size_t length,
                                      void * context);

typedef enum e_autosar_boot_validation
{
    AUTOSAR_BOOT_VALID = 0,
    AUTOSAR_BOOT_ERR_READER,
    AUTOSAR_BOOT_ERR_HEADER,
    AUTOSAR_BOOT_ERR_RANGE,
    AUTOSAR_BOOT_ERR_VECTOR,
    AUTOSAR_BOOT_ERR_CRC
} autosar_boot_validation_t;

uint32_t autosar_boot_crc32(uint8_t const * data, size_t length, uint32_t seed);
bool autosar_boot_validate_header(autosar_boot_image_header_t const * header);
bool autosar_boot_vector_valid(uint32_t stack_pointer, uint32_t reset_handler);
autosar_boot_validation_t autosar_boot_validate_slot(autosar_boot_read_fn reader,
                                                      void * context,
                                                      uint32_t slot_address,
                                                      uint32_t slot_size);

char const * autosar_boot_validation_name(autosar_boot_validation_t status);

#endif /* AUTOSAR_BOOT_H */
