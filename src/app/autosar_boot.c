#include "autosar_boot.h"

#include <string.h>

static bool autosar_boot_range_ok(uint32_t address, uint32_t length,
                                  uint32_t start, uint32_t size)
{
    uint64_t const end = (uint64_t) address + (uint64_t) length;
    uint64_t const limit = (uint64_t) start + (uint64_t) size;
    return ((uint64_t) address >= (uint64_t) start) && (end <= limit) && (end >= address);
}

uint32_t autosar_boot_crc32(uint8_t const * data, size_t length, uint32_t seed)
{
    uint32_t crc = ~seed;
    if ((NULL == data) && (length != 0U))
    {
        return 0U;
    }

    for (size_t index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            uint32_t const mask = (uint32_t) (-(int32_t) (crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool autosar_boot_vector_valid(uint32_t stack_pointer, uint32_t reset_handler)
{
    bool const stack_in_sram = (stack_pointer >= 0x22000000UL) &&
                               (stack_pointer < 0x22100000UL) &&
                               ((stack_pointer & 0x7U) == 0U);
    bool const reset_in_app = ((reset_handler & 1U) != 0U) &&
                              (reset_handler >= (AUTOSAR_APP_IMAGE_START | 1U)) &&
                              (reset_handler < (AUTOSAR_APP_SLOT_START + AUTOSAR_APP_SLOT_SIZE));
    return stack_in_sram && reset_in_app;
}

bool autosar_boot_validate_header(autosar_boot_image_header_t const * header)
{
    if (NULL == header)
    {
        return false;
    }
    if ((header->magic != AUTOSAR_BOOT_IMAGE_MAGIC) ||
        (header->header_size != AUTOSAR_BOOT_HEADER_SIZE) ||
        (header->format_version != AUTOSAR_BOOT_IMAGE_VERSION) ||
        (header->image_size == 0U) ||
        (header->image_size > (AUTOSAR_APP_SLOT_SIZE - AUTOSAR_BOOT_HEADER_SIZE)) ||
        !autosar_boot_vector_valid(header->vector_sp, header->vector_reset))
    {
        return false;
    }
    return true;
}

autosar_boot_validation_t autosar_boot_validate_slot(autosar_boot_read_fn reader,
                                                      void * context,
                                                      uint32_t slot_address,
                                                      uint32_t slot_size)
{
    autosar_boot_image_header_t header;
    uint8_t buffer[64U];
    uint32_t crc = 0U;
    uint32_t remaining;
    uint32_t address;

    if ((NULL == reader) || (slot_size < AUTOSAR_BOOT_HEADER_SIZE) ||
        !autosar_boot_range_ok(slot_address, slot_size, AUTOSAR_BOOT_FLASH_START,
                               AUTOSAR_BOOT_FLASH_SIZE))
    {
        return AUTOSAR_BOOT_ERR_READER;
    }
    if (!reader(slot_address, (uint8_t *) &header, sizeof(header), context))
    {
        return AUTOSAR_BOOT_ERR_READER;
    }
    if ((header.magic != AUTOSAR_BOOT_IMAGE_MAGIC) ||
        (header.header_size != AUTOSAR_BOOT_HEADER_SIZE) ||
        (header.format_version != AUTOSAR_BOOT_IMAGE_VERSION) ||
        (header.image_size == 0U))
    {
        return AUTOSAR_BOOT_ERR_HEADER;
    }
    if (!autosar_boot_vector_valid(header.vector_sp, header.vector_reset))
    {
        return AUTOSAR_BOOT_ERR_VECTOR;
    }
    if (header.image_size > (AUTOSAR_APP_SLOT_SIZE - AUTOSAR_BOOT_HEADER_SIZE))
    {
        return AUTOSAR_BOOT_ERR_RANGE;
    }
    if (header.image_size > (slot_size - AUTOSAR_BOOT_HEADER_SIZE))
    {
        return AUTOSAR_BOOT_ERR_RANGE;
    }

    remaining = header.image_size;
    address = slot_address + AUTOSAR_BOOT_HEADER_SIZE;
    while (remaining > 0U)
    {
        size_t const chunk = (remaining > sizeof(buffer)) ? sizeof(buffer) : (size_t) remaining;
        if (!reader(address, buffer, chunk, context))
        {
            return AUTOSAR_BOOT_ERR_READER;
        }
        crc = autosar_boot_crc32(buffer, chunk, crc);
        address += (uint32_t) chunk;
        remaining -= (uint32_t) chunk;
    }
    return (crc == header.image_crc32) ? AUTOSAR_BOOT_VALID : AUTOSAR_BOOT_ERR_CRC;
}

char const * autosar_boot_validation_name(autosar_boot_validation_t status)
{
    switch (status)
    {
        case AUTOSAR_BOOT_VALID:      return "VALID";
        case AUTOSAR_BOOT_ERR_READER: return "READER";
        case AUTOSAR_BOOT_ERR_HEADER: return "HEADER";
        case AUTOSAR_BOOT_ERR_RANGE:  return "RANGE";
        case AUTOSAR_BOOT_ERR_VECTOR: return "VECTOR";
        case AUTOSAR_BOOT_ERR_CRC:    return "CRC";
        default:                      return "UNKNOWN";
    }
}
