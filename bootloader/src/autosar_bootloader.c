#include "../include/autosar_bootloader_port.h"

autosar_bootloader_decision_t autosar_bootloader_select(autosar_bootloader_port_t const * port)
{
    autosar_boot_validation_t const validation = autosar_boot_validate_slot(
        (NULL != port) ? port->read_flash : NULL,
        (NULL != port) ? port->flash_context : NULL,
        AUTOSAR_APP_SLOT_START,
        AUTOSAR_APP_SLOT_SIZE);

    if ((NULL != port) && (NULL != port->update_requested) &&
        port->update_requested(port->context))
    {
        return AUTOSAR_BOOTLOADER_ENTER_UPDATE;
    }
    if (AUTOSAR_BOOT_VALID == validation)
    {
        return AUTOSAR_BOOTLOADER_JUMP_APPLICATION;
    }
    return AUTOSAR_BOOTLOADER_RECOVERY_REQUIRED;
}

bool autosar_bootloader_run(autosar_bootloader_port_t const * port)
{
    if (autosar_bootloader_select(port) != AUTOSAR_BOOTLOADER_JUMP_APPLICATION)
    {
        return false;
    }
    if ((NULL == port) || (NULL == port->jump_to_application))
    {
        return false;
    }
    return port->jump_to_application(AUTOSAR_APP_IMAGE_START, port->context);
}

autosar_bootloader_decision_t autosar_bootloader_select_secure(
    autosar_bootloader_port_t const * port,
    uint32_t minimum_security_counter)
{
    autosar_secure_image_header_t header;
    autosar_secure_validation_t validation;
    if ((NULL == port) || (NULL == port->read_flash) ||
        (NULL == port->hash_image) || (NULL == port->verify_signature) ||
        !port->read_flash(AUTOSAR_SECURE_SLOT_START, (uint8_t *) &header,
                           sizeof(header), port->flash_context))
    {
        return AUTOSAR_BOOTLOADER_RECOVERY_REQUIRED;
    }
    if ((NULL != port->update_requested) && port->update_requested(port->context))
    {
        return AUTOSAR_BOOTLOADER_ENTER_UPDATE;
    }
    validation = autosar_secure_validate(&header,
                                         AUTOSAR_SECURE_SLOT_START,
                                         AUTOSAR_SECURE_SLOT_SIZE,
                                         minimum_security_counter,
                                         port->hash_image,
                                         port->verify_signature,
                                         port->secure_context);
    return (AUTOSAR_SECURE_VALID == validation) ?
           AUTOSAR_BOOTLOADER_JUMP_APPLICATION : AUTOSAR_BOOTLOADER_RECOVERY_REQUIRED;
}

bool autosar_bootloader_run_secure(autosar_bootloader_port_t const * port,
                                   uint32_t minimum_security_counter)
{
    if (autosar_bootloader_select_secure(port, minimum_security_counter) !=
        AUTOSAR_BOOTLOADER_JUMP_APPLICATION)
    {
        return false;
    }
    if ((NULL == port) || (NULL == port->jump_to_application))
    {
        return false;
    }
    return port->jump_to_application(AUTOSAR_SECURE_APP_IMAGE_START, port->context);
}
