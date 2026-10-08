#include "../include/autosar_bootloader_port.h"

/* Platform entry point for the independent FSP project. CAN/UDS receive and
 * watchdog servicing stay in the project-specific main loop; this function
 * only makes the boot decision after clocks, RAM and FSP stacks are ready. */
autosar_bootloader_decision_t autosar_bootloader_entry(
    autosar_bootloader_port_t const * port,
    uint32_t minimum_security_counter)
{
    return autosar_bootloader_select_secure(port, minimum_security_counter);
}
