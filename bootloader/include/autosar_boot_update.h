#ifndef AUTOSAR_BOOT_UPDATE_H
#define AUTOSAR_BOOT_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "autosar_secure_image.h"

#define AUTOSAR_UPDATE_METADATA_MAGIC       (0x55504454UL) /* "UPDT" */
#define AUTOSAR_UPDATE_METADATA_VERSION     (1U)
#define AUTOSAR_UPDATE_METADATA_COMMITTED   (0xC0A117EDUL)
#define AUTOSAR_UPDATE_METADATA_SLOTS       (2U)
#define AUTOSAR_UPDATE_MAX_PROGRAM_UNIT    (128U)

typedef enum e_autosar_update_state
{
    AUTOSAR_UPDATE_IDLE = 0,
    AUTOSAR_UPDATE_ERASING,
    AUTOSAR_UPDATE_RECEIVING,
    AUTOSAR_UPDATE_VERIFYING,
    AUTOSAR_UPDATE_COMMITTING,
    AUTOSAR_UPDATE_COMPLETE,
    AUTOSAR_UPDATE_ERROR
} autosar_update_state_t;

typedef enum e_autosar_update_result
{
    AUTOSAR_UPDATE_OK = 0,
    AUTOSAR_UPDATE_ERR_ARGUMENT,
    AUTOSAR_UPDATE_ERR_STATE,
    AUTOSAR_UPDATE_ERR_RANGE,
    AUTOSAR_UPDATE_ERR_ROLLBACK,
    AUTOSAR_UPDATE_ERR_FLASH,
    AUTOSAR_UPDATE_ERR_VERIFY,
    AUTOSAR_UPDATE_ERR_METADATA
} autosar_update_result_t;

typedef struct st_autosar_update_metadata
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t state;
    uint32_t sequence;
    uint32_t security_counter;
    uint32_t image_version;
    uint32_t image_size;
    uint8_t image_hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
    uint32_t crc32;
    uint32_t commit_marker;
} autosar_update_metadata_t;

typedef bool (* autosar_update_erase_fn)(uint32_t address, uint32_t length,
                                         void * context);
typedef bool (* autosar_update_write_fn)(uint32_t address, uint8_t const * data,
                                         uint32_t length, void * context);
typedef bool (* autosar_update_metadata_read_fn)(uint32_t index,
                                                 autosar_update_metadata_t * record,
                                                 void * context);
/* The implementation must write the record body/CRC first and the commit
 * marker last, with a power-loss-safe flash programming granularity. */
typedef bool (* autosar_update_metadata_write_fn)(uint32_t index,
                                                  autosar_update_metadata_t const * record,
                                                  void * context);
typedef bool (* autosar_update_validate_fn)(autosar_secure_image_header_t const * header,
                                            void * context);
typedef bool (* autosar_update_counter_fn)(uint32_t target_security_counter,
                                           void * context);

typedef struct st_autosar_update_port
{
    uint32_t slot_address;
    uint32_t slot_size;
    autosar_update_erase_fn erase_slot;
    autosar_update_write_fn write_flash;
    autosar_update_metadata_read_fn read_metadata;
    autosar_update_metadata_write_fn write_metadata;
    autosar_update_validate_fn validate_image;
    autosar_update_counter_fn advance_counter;
    uint32_t write_granularity;
    void * context;
} autosar_update_port_t;

typedef struct st_autosar_update
{
    autosar_update_port_t port;
    autosar_update_state_t state;
    autosar_update_result_t last_error;
    uint32_t image_size;
    uint32_t image_version;
    uint32_t security_counter;
    uint32_t received;
    uint32_t minimum_security_counter;
    uint32_t active_sequence;
    uint32_t active_metadata_index;
    uint32_t pending_offset;
    uint32_t pending_length;
    uint32_t pending_words[AUTOSAR_UPDATE_MAX_PROGRAM_UNIT / sizeof(uint32_t)];
} autosar_update_t;

bool autosar_update_metadata_valid(autosar_update_metadata_t const * record);
bool autosar_update_metadata_select(autosar_update_metadata_t const * records,
                                    uint32_t * index,
                                    autosar_update_metadata_t * selected);
void autosar_update_init(autosar_update_t * update,
                         autosar_update_port_t const * port,
                         uint32_t minimum_security_counter);
autosar_update_result_t autosar_update_begin(autosar_update_t * update,
                                             uint32_t image_size,
                                             uint32_t image_version,
                                             uint32_t security_counter);
autosar_update_result_t autosar_update_write(autosar_update_t * update,
                                             uint32_t offset,
                                             uint8_t const * data,
                                             uint32_t length);
autosar_update_result_t autosar_update_finish(autosar_update_t * update,
                                              autosar_secure_image_header_t const * header);
autosar_update_state_t autosar_update_state(autosar_update_t const * update);
char const * autosar_update_result_name(autosar_update_result_t result);

#endif /* AUTOSAR_BOOT_UPDATE_H */
