#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/app/autosar_boot.h"
#include "../src/app/autosar_can_tp.h"
#include "../src/app/autosar_dcm.h"
#include "../bootloader/include/autosar_bootloader_port.h"
#include "../bootloader/include/autosar_secure_image.h"
#include "../bootloader/include/autosar_boot_update.h"
#include "../bootloader/include/autosar_boot_uds_update.h"

typedef struct
{
    uint8_t frames[8][8];
    uint8_t lengths[8];
    uint8_t count;
    uint8_t received[128];
    uint16_t received_length;
} test_io_t;

static bool test_tx(uint32_t can_id, uint8_t const * data, uint8_t length, void * context)
{
    test_io_t * io = (test_io_t *) context;
    assert(can_id == 0x7E8U);
    assert(io->count < 8U);
    memcpy(io->frames[io->count], data, length);
    io->lengths[io->count] = length;
    io->count++;
    return true;
}

static void test_rx(uint8_t const * data, uint16_t length, void * context)
{
    test_io_t * io = (test_io_t *) context;
    memcpy(io->received, data, length);
    io->received_length = length;
}

static void test_crc_and_header(void)
{
    static uint8_t const text[] = "123456789";
    autosar_boot_image_header_t header = {0};
    assert(autosar_boot_crc32(text, sizeof(text) - 1U, 0U) == 0xCBF43926UL);
    header.magic = AUTOSAR_BOOT_IMAGE_MAGIC;
    header.header_size = AUTOSAR_BOOT_HEADER_SIZE;
    header.format_version = AUTOSAR_BOOT_IMAGE_VERSION;
    header.image_size = 128U;
    header.vector_sp = 0x22001000UL;
    header.vector_reset = AUTOSAR_APP_IMAGE_START | 1U;
    assert(sizeof(header) == AUTOSAR_BOOT_HEADER_SIZE);
    assert(autosar_boot_validate_header(&header));
    header.vector_reset = AUTOSAR_APP_SLOT_START | 1U;
    assert(!autosar_boot_validate_header(&header));
}

static bool test_flash_reader(uint32_t address, uint8_t * data, size_t length, void * context)
{
    uint8_t const * flash = (uint8_t const *) context;
    uint32_t const offset = address - AUTOSAR_APP_SLOT_START;
    if ((address < AUTOSAR_APP_SLOT_START) || (offset > 256U) ||
        (length > (size_t) (256U - offset)))
    {
        return false;
    }
    memcpy(data, &flash[offset], length);
    return true;
}

static void test_slot_validation(void)
{
    uint8_t flash[256];
    uint8_t payload[32];
    autosar_boot_image_header_t header;
    memset(flash, 0xFF, sizeof(flash));
    for (uint8_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t) (index * 3U);
    }
    memset(&header, 0, sizeof(header));
    header.magic = AUTOSAR_BOOT_IMAGE_MAGIC;
    header.header_size = AUTOSAR_BOOT_HEADER_SIZE;
    header.format_version = AUTOSAR_BOOT_IMAGE_VERSION;
    header.image_size = sizeof(payload);
    header.image_version = 0x10000U;
    header.image_crc32 = autosar_boot_crc32(payload, sizeof(payload), 0U);
    header.vector_sp = 0x22001000UL;
    header.vector_reset = AUTOSAR_APP_IMAGE_START | 1U;
    memcpy(flash, &header, sizeof(header));
    memcpy(&flash[AUTOSAR_BOOT_HEADER_SIZE], payload, sizeof(payload));
    assert(autosar_boot_validate_slot(test_flash_reader, flash,
                                      AUTOSAR_APP_SLOT_START, sizeof(flash)) == AUTOSAR_BOOT_VALID);
    flash[AUTOSAR_BOOT_HEADER_SIZE + 1U] ^= 0x01U;
    assert(autosar_boot_validate_slot(test_flash_reader, flash,
                                      AUTOSAR_APP_SLOT_START, sizeof(flash)) == AUTOSAR_BOOT_ERR_CRC);
}

static bool test_no_update(void * context)
{
    (void) context;
    return false;
}

static bool test_jump(uint32_t vector_address, void * context)
{
    bool * jumped = (bool *) context;
    assert(vector_address == AUTOSAR_APP_IMAGE_START);
    *jumped = true;
    return true;
}

static bool test_secure_jump(uint32_t vector_address, void * context)
{
    bool * jumped = (bool *) context;
    assert(vector_address == AUTOSAR_SECURE_APP_IMAGE_START);
    *jumped = true;
    return true;
}

static void test_bootloader_select(void)
{
    uint8_t flash[256];
    uint8_t payload[16];
    autosar_boot_image_header_t header;
    bool jumped = false;
    autosar_bootloader_port_t port;
    memset(flash, 0xFF, sizeof(flash));
    memset(payload, 0x5A, sizeof(payload));
    memset(&header, 0, sizeof(header));
    header.magic = AUTOSAR_BOOT_IMAGE_MAGIC;
    header.header_size = AUTOSAR_BOOT_HEADER_SIZE;
    header.format_version = AUTOSAR_BOOT_IMAGE_VERSION;
    header.image_size = sizeof(payload);
    header.image_crc32 = autosar_boot_crc32(payload, sizeof(payload), 0U);
    header.vector_sp = 0x22002000UL;
    header.vector_reset = AUTOSAR_APP_IMAGE_START | 1U;
    memcpy(flash, &header, sizeof(header));
    memcpy(&flash[AUTOSAR_BOOT_HEADER_SIZE], payload, sizeof(payload));
    memset(&port, 0, sizeof(port));
    port.read_flash = test_flash_reader;
    port.flash_context = flash;
    port.update_requested = test_no_update;
    port.jump_to_application = test_jump;
    port.context = &jumped;
    assert(autosar_bootloader_select(&port) == AUTOSAR_BOOTLOADER_JUMP_APPLICATION);
    assert(autosar_bootloader_run(&port));
    assert(jumped);
}

typedef struct
{
    uint8_t const * flash;
    uint8_t signing_hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
} secure_test_context_t;

static bool test_secure_hash(uint32_t address, uint32_t length,
                             uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE], void * context)
{
    secure_test_context_t const * ctx = (secure_test_context_t const *) context;
    autosar_sha256(&ctx->flash[address - AUTOSAR_SECURE_SLOT_START], length, hash);
    return true;
}

static bool test_secure_verify(uint8_t const hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE],
                               uint8_t const signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE],
                               uint32_t key_id, void * context)
{
    secure_test_context_t const * ctx = (secure_test_context_t const *) context;
    bool const signature_ok = (key_id == AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION) &&
                              (signature[0] == 0xA5U) && (signature[63] == 0x5AU);
    return signature_ok && (memcmp(hash, ctx->signing_hash, sizeof(ctx->signing_hash)) == 0);
}

static void test_secure_image(void)
{
    uint8_t flash[512];
    uint8_t payload[24];
    autosar_secure_image_header_t header;
    secure_test_context_t context;
    memset(flash, 0xFF, sizeof(flash));
    for (uint8_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t) (0x30U + index);
    }
    memset(&header, 0, sizeof(header));
    header.magic = AUTOSAR_SECURE_IMAGE_MAGIC;
    header.header_size = AUTOSAR_SECURE_IMAGE_HEADER_SIZE;
    header.format_version = AUTOSAR_SECURE_IMAGE_FORMAT_VERSION;
    header.image_size = sizeof(payload);
    header.image_version = 0x20000U;
    header.security_counter = 5U;
    header.vector_sp = 0x22002000UL;
    header.vector_reset = AUTOSAR_SECURE_APP_IMAGE_START | 1U;
    header.key_id = AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION;
    autosar_sha256(payload, sizeof(payload), header.image_hash);
    memset(header.signature, 0xA5, sizeof(header.signature));
    header.signature[63] = 0x5AU;
    memcpy(flash, &header, sizeof(header));
    memcpy(&flash[AUTOSAR_SECURE_IMAGE_HEADER_SIZE], payload, sizeof(payload));
    context.flash = flash;
    autosar_secure_signing_digest(&header, context.signing_hash);
    assert(sizeof(header) == AUTOSAR_SECURE_IMAGE_HEADER_SIZE);
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(flash), 5U, test_secure_hash,
                                   test_secure_verify, &context) == AUTOSAR_SECURE_VALID);
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(flash), 6U, test_secure_hash,
                                   test_secure_verify, &context) == AUTOSAR_SECURE_ERR_ROLLBACK);
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   AUTOSAR_SECURE_IMAGE_HEADER_SIZE - 1U, 5U,
                                   test_secure_hash, test_secure_verify,
                                   &context) == AUTOSAR_SECURE_ERR_RANGE);
    flash[AUTOSAR_SECURE_IMAGE_HEADER_SIZE] ^= 1U;
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(flash), 5U, test_secure_hash,
                                   test_secure_verify, &context) == AUTOSAR_SECURE_ERR_HASH);

    memcpy(flash, &header, sizeof(header));
    memcpy(&flash[AUTOSAR_SECURE_IMAGE_HEADER_SIZE], payload, sizeof(payload));
    header.vector_reset = AUTOSAR_SECURE_SLOT_START | 1U;
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(flash), 5U, test_secure_hash,
                                   test_secure_verify, &context) == AUTOSAR_SECURE_ERR_VECTOR);
    header.vector_reset = AUTOSAR_SECURE_APP_IMAGE_START | 1U;
    header.signature[0] = 0U;
    autosar_secure_signing_digest(&header, context.signing_hash);
    assert(autosar_secure_validate(&header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(flash), 5U, test_secure_hash,
                                   test_secure_verify, &context) == AUTOSAR_SECURE_ERR_SIGNATURE);

    header.signature[0] = 0xA5U;
    autosar_secure_signing_digest(&header, context.signing_hash);
    memcpy(flash, &header, sizeof(header));
    memcpy(&flash[AUTOSAR_SECURE_IMAGE_HEADER_SIZE], payload, sizeof(payload));
    bool jumped = false;
    autosar_bootloader_port_t port;
    memset(&port, 0, sizeof(port));
    port.read_flash = test_flash_reader;
    port.flash_context = flash;
    port.update_requested = test_no_update;
    port.jump_to_application = test_secure_jump;
    port.context = &jumped;
    port.hash_image = test_secure_hash;
    port.verify_signature = test_secure_verify;
    port.secure_context = &context;
    assert(autosar_bootloader_select_secure(&port, 5U) == AUTOSAR_BOOTLOADER_JUMP_APPLICATION);
    assert(autosar_bootloader_run_secure(&port, 5U));
    assert(jumped);
}

typedef struct
{
    uint8_t flash[512];
    autosar_update_metadata_t metadata[AUTOSAR_UPDATE_METADATA_SLOTS];
    secure_test_context_t secure;
} update_test_context_t;

static bool test_update_erase(uint32_t address, uint32_t length, void * context)
{
    update_test_context_t * test = (update_test_context_t *) context;
    if ((address != AUTOSAR_SECURE_SLOT_START) || (length != sizeof(test->flash)))
    {
        return false;
    }
    memset(test->flash, 0xFF, sizeof(test->flash));
    return true;
}

static bool test_update_write(uint32_t address, uint8_t const * data,
                              uint32_t length, void * context)
{
    update_test_context_t * test = (update_test_context_t *) context;
    uint32_t const offset = address - AUTOSAR_SECURE_SLOT_START;
    if ((address < AUTOSAR_SECURE_SLOT_START) || (offset > sizeof(test->flash)) ||
        (length > (sizeof(test->flash) - offset)))
    {
        return false;
    }
    memcpy(&test->flash[offset], data, length);
    return true;
}

static bool test_update_read_metadata(uint32_t index,
                                      autosar_update_metadata_t * record,
                                      void * context)
{
    update_test_context_t const * test = (update_test_context_t const *) context;
    if ((index >= AUTOSAR_UPDATE_METADATA_SLOTS) || (NULL == record))
    {
        return false;
    }
    *record = test->metadata[index];
    return true;
}

static bool test_update_write_metadata(uint32_t index,
                                       autosar_update_metadata_t const * record,
                                       void * context)
{
    update_test_context_t * test = (update_test_context_t *) context;
    if ((index >= AUTOSAR_UPDATE_METADATA_SLOTS) || (NULL == record))
    {
        return false;
    }
    test->metadata[index] = *record;
    return true;
}

static bool test_update_validate(autosar_secure_image_header_t const * header,
                                 void * context)
{
    update_test_context_t * test = (update_test_context_t *) context;
    test->secure.flash = test->flash;
    autosar_secure_signing_digest(header, test->secure.signing_hash);
    return autosar_secure_validate(header, AUTOSAR_SECURE_SLOT_START,
                                   sizeof(test->flash), 5U,
                                   test_secure_hash, test_secure_verify,
                                   &test->secure) == AUTOSAR_SECURE_VALID;
}

static void test_secure_update(void)
{
    update_test_context_t context;
    autosar_update_port_t port;
    autosar_update_t update;
    autosar_secure_image_header_t header;
    uint8_t payload[24];
    memset(&context, 0, sizeof(context));
    memset(context.flash, 0xFF, sizeof(context.flash));
    for (uint8_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t) (0x40U + index);
    }
    memset(&header, 0, sizeof(header));
    header.magic = AUTOSAR_SECURE_IMAGE_MAGIC;
    header.header_size = AUTOSAR_SECURE_IMAGE_HEADER_SIZE;
    header.format_version = AUTOSAR_SECURE_IMAGE_FORMAT_VERSION;
    header.image_size = sizeof(payload);
    header.image_version = 0x30000U;
    header.security_counter = 5U;
    header.vector_sp = 0x22002000UL;
    header.vector_reset = AUTOSAR_SECURE_APP_IMAGE_START | 1U;
    header.key_id = AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION;
    autosar_sha256(payload, sizeof(payload), header.image_hash);
    memset(header.signature, 0xA5, sizeof(header.signature));
    header.signature[63] = 0x5AU;
    context.secure.flash = context.flash;
    autosar_secure_signing_digest(&header, context.secure.signing_hash);
    memset(&port, 0, sizeof(port));
    port.slot_address = AUTOSAR_SECURE_SLOT_START;
    port.slot_size = sizeof(context.flash);
    port.erase_slot = test_update_erase;
    port.write_flash = test_update_write;
    port.read_metadata = test_update_read_metadata;
    port.write_metadata = test_update_write_metadata;
    port.validate_image = test_update_validate;
    port.write_granularity = 4U;
    port.context = &context;
    autosar_update_init(&update, &port, 5U);
    assert(autosar_update_begin(&update, sizeof(payload), header.image_version, 5U) ==
           AUTOSAR_UPDATE_OK);
    assert(autosar_update_write(&update, 0U, payload, 10U) == AUTOSAR_UPDATE_OK);
    assert(autosar_update_write(&update, 10U, &payload[10], sizeof(payload) - 10U) ==
           AUTOSAR_UPDATE_OK);
    assert(autosar_update_finish(&update, &header) == AUTOSAR_UPDATE_OK);
    assert(autosar_update_state(&update) == AUTOSAR_UPDATE_COMPLETE);
    assert(autosar_update_metadata_valid(&context.metadata[1]));
    assert(autosar_update_begin(&update, sizeof(payload), header.image_version, 4U) ==
           AUTOSAR_UPDATE_ERR_ROLLBACK);
}

static void test_store_be32(uint8_t * data, uint32_t value)
{
    data[0] = (uint8_t) (value >> 24U);
    data[1] = (uint8_t) (value >> 16U);
    data[2] = (uint8_t) (value >> 8U);
    data[3] = (uint8_t) value;
}

static void test_secure_uds_update(void)
{
    update_test_context_t context;
    autosar_update_port_t port;
    autosar_update_t update;
    autosar_boot_uds_update_t uds;
    uint8_t payload[24];
    uint8_t request[128];
    uint8_t response[8];
    uint16_t response_length = 0U;
    uint8_t hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
    uint8_t signature[AUTOSAR_SECURE_IMAGE_SIGNATURE_SIZE];
    memset(&context, 0, sizeof(context));
    memset(context.flash, 0xFF, sizeof(context.flash));
    memset(payload, 0x6BU, sizeof(payload));
    autosar_sha256(payload, sizeof(payload), hash);
    memset(signature, 0xA5, sizeof(signature));
    signature[63] = 0x5AU;
    memset(&port, 0, sizeof(port));
    port.slot_address = AUTOSAR_SECURE_SLOT_START;
    port.slot_size = sizeof(context.flash);
    port.erase_slot = test_update_erase;
    port.write_flash = test_update_write;
    port.write_metadata = test_update_write_metadata;
    port.validate_image = test_update_validate;
    port.write_granularity = 4U;
    port.context = &context;
    autosar_update_init(&update, &port, 5U);
    autosar_boot_uds_update_init(&uds, &update);
    memset(request, 0, sizeof(request));
    request[0] = AUTOSAR_BOOT_UDS_SERVICE_REQUEST_DOWNLOAD;
    test_store_be32(&request[3], sizeof(payload));
    test_store_be32(&request[7], 0x40000U);
    test_store_be32(&request[11], 5U);
    test_store_be32(&request[15], 0x22002000UL);
    test_store_be32(&request[19], AUTOSAR_SECURE_APP_IMAGE_START | 1U);
    test_store_be32(&request[23], 0U);
    test_store_be32(&request[27], AUTOSAR_SECURE_IMAGE_KEY_ID_PRODUCTION);
    memcpy(&request[31], hash, sizeof(hash));
    memcpy(&request[63], signature, sizeof(signature));
    assert(autosar_boot_uds_update_process(&uds, request, 127U, response,
                                           sizeof(response), &response_length));
    assert(response[0] == 0x74U);
    request[0] = AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_DATA;
    request[1] = 1U;
    memcpy(&request[2], payload, 12U);
    assert(autosar_boot_uds_update_process(&uds, request, 14U, response,
                                           sizeof(response), &response_length));
    request[1] = 2U;
    memcpy(&request[2], &payload[12], sizeof(payload) - 12U);
    assert(autosar_boot_uds_update_process(&uds, request, 14U, response,
                                           sizeof(response), &response_length));
    request[0] = AUTOSAR_BOOT_UDS_SERVICE_TRANSFER_EXIT;
    assert(autosar_boot_uds_update_process(&uds, request, 1U, response,
                                           sizeof(response), &response_length));
    assert(response[0] == 0x77U);
    assert(autosar_update_state(&update) == AUTOSAR_UPDATE_COMPLETE);
}

static void test_dcm_single_frame(void)
{
    autosar_dcm_t dcm;
    uint8_t request[] = {0x10U, 0x03U};
    uint8_t response[32];
    uint16_t response_length = 0U;
    autosar_dcm_init(&dcm);
    assert(autosar_dcm_process(&dcm, request, sizeof(request), response,
                               sizeof(response), &response_length));
    assert(response_length == 6U);
    assert(response[0] == 0x50U && response[1] == 0x03U);
    assert(dcm.session == AUTOSAR_DCM_SESSION_EXTENDED);
}

static void test_can_tp_and_long_uds(void)
{
    autosar_can_tp_t tp;
    test_io_t io;
    uint8_t request[8] = {0x03U, 0x22U, 0xF1U, 0x90U, 0U, 0U, 0U, 0U};
    memset(&io, 0, sizeof(io));
    autosar_can_tp_init(&tp, 0x7E0U, 0x7E8U, test_tx, test_rx, &io);
    assert(autosar_can_tp_on_can_frame(&tp, 0x7E0U, request, 8U, 10U));
    assert(io.received_length == 3U);
    assert(io.received[0] == 0x22U && io.received[2] == 0x90U);

    uint8_t payload[24];
    for (uint8_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = index;
    }
    io.count = 0U;
    assert(autosar_can_tp_send(&tp, payload, sizeof(payload)));
    assert(io.count == 4U);
    assert((io.frames[0][0] & 0xF0U) == 0x10U);
    assert((io.frames[1][0] & 0xF0U) == 0x20U);
    assert(io.frames[3][1] == 20U);
}

int main(void)
{
    test_crc_and_header();
    test_slot_validation();
    test_bootloader_select();
    test_secure_image();
    test_secure_update();
    test_secure_uds_update();
    test_dcm_single_frame();
    test_can_tp_and_long_uds();
    puts("AUTOSAR CAN/bootloader tests: PASS");
    return 0;
}
