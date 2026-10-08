#include "autosar_stack.h"

#include <string.h>

#include "autosar_can_tp.h"
#include "can_bus/can_bus_port.h"

#define AUTOSAR_UDS_REQUEST_ID       (0x7E0U)
#define AUTOSAR_UDS_RESPONSE_ID      (0x7E8U)

static autosar_can_tp_t g_tp;
static autosar_dcm_t g_dcm;
static bool g_initialized;
static uint32_t g_rx_requests;
static uint32_t g_tx_responses;

static bool autosar_stack_send(uint32_t can_id, uint8_t const * data,
                               uint8_t length, void * context)
{
    (void) context;
    return (FSP_SUCCESS == can_bus_port_send(can_id, CAN_ID_MODE_STANDARD,
                                              false, false, data, length));
}

static void autosar_stack_dcm_rx(uint8_t const * request, uint16_t request_length,
                                 void * context)
{
    uint8_t response[AUTOSAR_DCM_MAX_RESPONSE];
    uint16_t response_length = 0U;
    (void) context;
    g_rx_requests++;
    if (autosar_dcm_process(&g_dcm, request, request_length, response,
                            (uint16_t) sizeof(response), &response_length) &&
        autosar_can_tp_send(&g_tp, response, response_length))
    {
        g_tx_responses++;
    }
}

void autosar_stack_init(void)
{
    autosar_dcm_init(&g_dcm);
    autosar_can_tp_init(&g_tp, AUTOSAR_UDS_REQUEST_ID, AUTOSAR_UDS_RESPONSE_ID,
                        autosar_stack_send, autosar_stack_dcm_rx, NULL);
    g_rx_requests = 0U;
    g_tx_responses = 0U;
    g_initialized = true;
}

void autosar_stack_main_function(uint32_t now_ms)
{
    if (g_initialized)
    {
        autosar_can_tp_main_function(&g_tp, now_ms);
    }
}

bool autosar_stack_on_can_frame(uint32_t can_id,
                                uint8_t const * data,
                                uint8_t length,
                                uint32_t now_ms)
{
    if (!g_initialized)
    {
        return false;
    }
    return autosar_can_tp_on_can_frame(&g_tp, can_id, data, length, now_ms);
}

void autosar_stack_get_status(autosar_stack_status_t * status)
{
    if (NULL == status)
    {
        return;
    }
    status->initialized = g_initialized;
    status->rx_requests = g_rx_requests;
    status->tx_responses = g_tx_responses;
    status->session = g_dcm.session;
    status->boot_validation = g_dcm.last_boot_validation;
}
