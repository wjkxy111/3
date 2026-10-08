#include "../include/autosar_boot_update.h"

#include <stddef.h>
#include <string.h>

#include "../../src/app/autosar_boot.h"

static uint32_t autosar_update_metadata_crc(autosar_update_metadata_t const * record)
{
    return autosar_boot_crc32((uint8_t const *) record,
                              offsetof(autosar_update_metadata_t, crc32), 0U);
}

bool autosar_update_metadata_valid(autosar_update_metadata_t const * record)
{
    if ((NULL == record) || (record->magic != AUTOSAR_UPDATE_METADATA_MAGIC) ||
        (record->format_version != AUTOSAR_UPDATE_METADATA_VERSION) ||
        (record->state != 1U) ||
        (record->commit_marker != AUTOSAR_UPDATE_METADATA_COMMITTED))
    {
        return false;
    }
    return record->crc32 == autosar_update_metadata_crc(record);
}

bool autosar_update_metadata_select(autosar_update_metadata_t const * records,
                                    uint32_t * index,
                                    autosar_update_metadata_t * selected)
{
    bool const first_valid = (NULL != records) && autosar_update_metadata_valid(&records[0]);
    bool const second_valid = (NULL != records) && autosar_update_metadata_valid(&records[1]);
    uint32_t chosen;
    if ((!first_valid) && (!second_valid))
    {
        return false;
    }
    if (!first_valid)
    {
        chosen = 1U;
    }
    else if (!second_valid)
    {
        chosen = 0U;
    }
    else
    {
        chosen = (records[1].sequence > records[0].sequence) ? 1U : 0U;
    }
    if (NULL != index)
    {
        *index = chosen;
    }
    if (NULL != selected)
    {
        *selected = records[chosen];
    }
    return true;
}

static autosar_update_result_t autosar_update_fail(autosar_update_t * update,
                                                    autosar_update_result_t result)
{
    update->last_error = result;
    update->state = AUTOSAR_UPDATE_ERROR;
    return result;
}

static uint8_t * autosar_update_pending_bytes(autosar_update_t * update)
{
    return (uint8_t *) update->pending_words;
}

static autosar_update_result_t autosar_update_flush_pending(autosar_update_t * update,
                                                             bool final_block)
{
    uint32_t unit = update->port.write_granularity;
    uint8_t * pending = autosar_update_pending_bytes(update);
    if (unit <= 1U)
    {
        return AUTOSAR_UPDATE_OK;
    }
    if (final_block && (update->pending_length > 0U))
    {
        memset(&pending[update->pending_length], 0xFF,
               unit - update->pending_length);
        update->pending_length = unit;
    }
    if (update->pending_length == 0U)
    {
        return AUTOSAR_UPDATE_OK;
    }
    if (update->pending_length != unit)
    {
        return AUTOSAR_UPDATE_OK;
    }
    if (!update->port.write_flash(update->port.slot_address +
                                  AUTOSAR_SECURE_IMAGE_HEADER_SIZE + update->pending_offset,
                                  pending, unit, update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_FLASH);
    }
    update->pending_length = 0U;
    return AUTOSAR_UPDATE_OK;
}

void autosar_update_init(autosar_update_t * update,
                         autosar_update_port_t const * port,
                         uint32_t minimum_security_counter)
{
    autosar_update_metadata_t records[AUTOSAR_UPDATE_METADATA_SLOTS];
    autosar_update_metadata_t selected;
    bool have_metadata = false;
    if (NULL == update)
    {
        return;
    }
    memset(update, 0, sizeof(*update));
    if (NULL != port)
    {
        update->port = *port;
    }
    update->minimum_security_counter = minimum_security_counter;
    update->state = AUTOSAR_UPDATE_IDLE;
    update->last_error = AUTOSAR_UPDATE_OK;
    update->active_metadata_index = 0U;
    if ((NULL != port) && (NULL != port->read_metadata))
    {
        memset(records, 0, sizeof(records));
        for (uint32_t index = 0U; index < AUTOSAR_UPDATE_METADATA_SLOTS; index++)
        {
            if (!port->read_metadata(index, &records[index], port->context))
            {
                records[index].magic = 0U;
            }
        }
        have_metadata = autosar_update_metadata_select(records,
                                                        &update->active_metadata_index,
                                                        &selected);
        if (have_metadata &&
            (selected.security_counter > update->minimum_security_counter))
        {
            update->minimum_security_counter = selected.security_counter;
        }
        if (have_metadata)
        {
            update->active_sequence = selected.sequence;
        }
    }
}

autosar_update_result_t autosar_update_begin(autosar_update_t * update,
                                             uint32_t image_size,
                                             uint32_t image_version,
                                             uint32_t security_counter)
{
    uint32_t const payload_capacity =
        (update != NULL) && (update->port.slot_size >= AUTOSAR_SECURE_IMAGE_HEADER_SIZE) ?
        (update->port.slot_size - AUTOSAR_SECURE_IMAGE_HEADER_SIZE) : 0U;
    if ((NULL == update) || (NULL == update->port.erase_slot) ||
        (NULL == update->port.write_flash) || (NULL == update->port.validate_image) ||
        (image_size == 0U))
    {
        return (NULL == update) ? AUTOSAR_UPDATE_ERR_ARGUMENT :
               autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ARGUMENT);
    }
    if ((update->state != AUTOSAR_UPDATE_IDLE) &&
        (update->state != AUTOSAR_UPDATE_COMPLETE) &&
        (update->state != AUTOSAR_UPDATE_ERROR))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_STATE);
    }
    if (image_size > payload_capacity)
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_RANGE);
    }
    if (security_counter < update->minimum_security_counter)
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ROLLBACK);
    }
    update->image_size = image_size;
    update->image_version = image_version;
    update->security_counter = security_counter;
    update->received = 0U;
    update->pending_offset = 0U;
    update->pending_length = 0U;
    if (update->port.write_granularity > AUTOSAR_UPDATE_MAX_PROGRAM_UNIT)
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ARGUMENT);
    }
    update->last_error = AUTOSAR_UPDATE_OK;
    update->state = AUTOSAR_UPDATE_ERASING;
    if (!update->port.erase_slot(update->port.slot_address, update->port.slot_size,
                                 update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_FLASH);
    }
    update->state = AUTOSAR_UPDATE_RECEIVING;
    return AUTOSAR_UPDATE_OK;
}

autosar_update_result_t autosar_update_write(autosar_update_t * update,
                                             uint32_t offset,
                                             uint8_t const * data,
                                             uint32_t length)
{
    uint32_t address;
    uint32_t unit;
    uint32_t consumed = 0U;
    uint8_t * pending;
    if ((NULL == update) || ((NULL == data) && (length != 0U)))
    {
        return (NULL == update) ? AUTOSAR_UPDATE_ERR_ARGUMENT :
               autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ARGUMENT);
    }
    if (update->state != AUTOSAR_UPDATE_RECEIVING)
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_STATE);
    }
    if ((offset != update->received) || (length == 0U) ||
        (update->received > update->image_size) ||
        (length > (update->image_size - update->received)))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_RANGE);
    }
    unit = update->port.write_granularity;
    if (unit <= 1U)
    {
        address = update->port.slot_address + AUTOSAR_SECURE_IMAGE_HEADER_SIZE + offset;
        if (!update->port.write_flash(address, data, length, update->port.context))
        {
            return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_FLASH);
        }
        update->received += length;
        return AUTOSAR_UPDATE_OK;
    }
    pending = autosar_update_pending_bytes(update);
    while (consumed < length)
    {
        uint32_t const room = unit - update->pending_length;
        uint32_t const remaining = length - consumed;
        uint32_t const take = (remaining < room) ? remaining : room;
        if (0U == update->pending_length)
        {
            update->pending_offset = update->received;
        }
        memcpy(&pending[update->pending_length], &data[consumed], take);
        update->pending_length += take;
        update->received += take;
        consumed += take;
        if (update->pending_length == unit)
        {
            if (AUTOSAR_UPDATE_OK != autosar_update_flush_pending(update, false))
            {
                return update->last_error;
            }
        }
    }
    return AUTOSAR_UPDATE_OK;
}

autosar_update_result_t autosar_update_finish(autosar_update_t * update,
                                              autosar_secure_image_header_t const * header)
{
    autosar_update_metadata_t record;
    uint32_t const next_index = (update != NULL) ?
                                ((update->active_metadata_index + 1U) % AUTOSAR_UPDATE_METADATA_SLOTS) : 0U;
    if ((NULL == update) || (NULL == header))
    {
        return (NULL == update) ? AUTOSAR_UPDATE_ERR_ARGUMENT :
               autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ARGUMENT);
    }
    if ((update->state != AUTOSAR_UPDATE_RECEIVING) ||
        (update->received != update->image_size))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_STATE);
    }
    if (AUTOSAR_UPDATE_OK != autosar_update_flush_pending(update, true))
    {
        return update->last_error;
    }
    if ((header->image_size != update->image_size) ||
        (header->image_version != update->image_version) ||
        (header->security_counter != update->security_counter))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_VERIFY);
    }
    /* The header is written only after all payload blocks arrived.  The
     * validator must read it and the payload back from flash; callers must
     * not validate an in-RAM header alone. */
    if (!update->port.write_flash(update->port.slot_address,
                                  (uint8_t const *) header,
                                  (uint32_t) sizeof(*header),
                                  update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_FLASH);
    }
    update->state = AUTOSAR_UPDATE_VERIFYING;
    if (!update->port.validate_image(header, update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_VERIFY);
    }
    /* If bound, advance the one-way hardware counter before publishing the
     * metadata record. A reset after this point can only force recovery or
     * install an image with the same/newer counter; it cannot boot the old
     * image. */
    if ((NULL != update->port.advance_counter) &&
        !update->port.advance_counter(header->security_counter, update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_ROLLBACK);
    }
    if (NULL == update->port.write_metadata)
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_METADATA);
    }
    memset(&record, 0, sizeof(record));
    record.magic = AUTOSAR_UPDATE_METADATA_MAGIC;
    record.format_version = AUTOSAR_UPDATE_METADATA_VERSION;
    record.state = 1U;
    record.sequence = update->active_sequence + 1U;
    record.security_counter = header->security_counter;
    record.image_version = header->image_version;
    record.image_size = header->image_size;
    memcpy(record.image_hash, header->image_hash, sizeof(record.image_hash));
    record.crc32 = autosar_update_metadata_crc(&record);
    record.commit_marker = AUTOSAR_UPDATE_METADATA_COMMITTED;
    update->state = AUTOSAR_UPDATE_COMMITTING;
    if (!update->port.write_metadata(next_index, &record, update->port.context))
    {
        return autosar_update_fail(update, AUTOSAR_UPDATE_ERR_METADATA);
    }
    update->active_metadata_index = next_index;
    update->active_sequence = record.sequence;
    update->minimum_security_counter = record.security_counter;
    update->state = AUTOSAR_UPDATE_COMPLETE;
    return AUTOSAR_UPDATE_OK;
}

autosar_update_state_t autosar_update_state(autosar_update_t const * update)
{
    return (NULL != update) ? update->state : AUTOSAR_UPDATE_ERROR;
}

char const * autosar_update_result_name(autosar_update_result_t result)
{
    switch (result)
    {
        case AUTOSAR_UPDATE_OK:                  return "OK";
        case AUTOSAR_UPDATE_ERR_ARGUMENT:        return "ARGUMENT";
        case AUTOSAR_UPDATE_ERR_STATE:           return "STATE";
        case AUTOSAR_UPDATE_ERR_RANGE:           return "RANGE";
        case AUTOSAR_UPDATE_ERR_ROLLBACK:        return "ROLLBACK";
        case AUTOSAR_UPDATE_ERR_FLASH:           return "FLASH";
        case AUTOSAR_UPDATE_ERR_VERIFY:          return "VERIFY";
        case AUTOSAR_UPDATE_ERR_METADATA:        return "METADATA";
        default:                                 return "UNKNOWN";
    }
}
