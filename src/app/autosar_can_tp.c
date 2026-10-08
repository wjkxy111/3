#include "autosar_can_tp.h"

#include <string.h>

static bool autosar_can_tp_send_frame(autosar_can_tp_t * tp,
                                      uint8_t const * data,
                                      uint8_t length)
{
    return (NULL != tp->tx) && tp->tx(tp->tx_id, data, length, tp->context);
}

void autosar_can_tp_init(autosar_can_tp_t * tp,
                         uint32_t rx_id,
                         uint32_t tx_id,
                         autosar_can_tp_tx_fn tx,
                         autosar_can_tp_rx_fn rx,
                         void * context)
{
    if (NULL == tp)
    {
        return;
    }
    memset(tp, 0, sizeof(*tp));
    tp->rx_id = rx_id;
    tp->tx_id = tx_id;
    tp->tx = tx;
    tp->rx = rx;
    tp->context = context;
}

bool autosar_can_tp_send(autosar_can_tp_t * tp, uint8_t const * data, uint16_t length)
{
    uint8_t frame[8] = {0U};
    uint16_t offset = 0U;
    uint8_t sequence = 1U;

    if ((NULL == tp) || ((NULL == data) && (length != 0U)) ||
        (length == 0U) || (length > AUTOSAR_CAN_TP_MAX_PAYLOAD))
    {
        return false;
    }
    if (length <= 7U)
    {
        frame[0] = (uint8_t) length;
        memcpy(&frame[1], data, length);
        return autosar_can_tp_send_frame(tp, frame, 8U);
    }

    frame[0] = (uint8_t) (0x10U | ((length >> 8U) & 0x0FU));
    frame[1] = (uint8_t) length;
    memcpy(&frame[2], data, 6U);
    if (!autosar_can_tp_send_frame(tp, frame, 8U))
    {
        return false;
    }
    offset = 6U;
    while (offset < length)
    {
        uint16_t const remaining = length - offset;
        uint8_t const chunk = (remaining > 7U) ? 7U : (uint8_t) remaining;
        memset(frame, 0, sizeof(frame));
        frame[0] = (uint8_t) (0x20U | (sequence & 0x0FU));
        memcpy(&frame[1], &data[offset], chunk);
        if (!autosar_can_tp_send_frame(tp, frame, 8U))
        {
            return false;
        }
        offset = (uint16_t) (offset + chunk);
        sequence = (uint8_t) ((sequence + 1U) & 0x0FU);
        if (sequence == 0U)
        {
            sequence = 1U;
        }
    }
    return true;
}

static void autosar_can_tp_complete(autosar_can_tp_t * tp)
{
    if (NULL != tp->rx)
    {
        tp->rx(tp->buffer, tp->received_length, tp->context);
    }
    tp->receiving = false;
    tp->expected_length = 0U;
    tp->received_length = 0U;
}

bool autosar_can_tp_on_can_frame(autosar_can_tp_t * tp,
                                 uint32_t can_id,
                                 uint8_t const * data,
                                 uint8_t length,
                                 uint32_t now_ms)
{
    uint8_t pci;
    if ((NULL == tp) || (can_id != tp->rx_id) || (NULL == data) || (length == 0U))
    {
        return false;
    }
    pci = (uint8_t) (data[0] >> 4U);
    tp->last_rx_ms = now_ms;

    if (pci == 0U)
    {
        uint8_t const payload_length = (uint8_t) (data[0] & 0x0FU);
        if ((payload_length == 0U) || (payload_length > 7U) ||
            (payload_length > (uint8_t) (length - 1U)))
        {
            return false;
        }
        if (NULL != tp->rx)
        {
            tp->rx(&data[1], payload_length, tp->context);
        }
        return true;
    }

    if (pci == 1U)
    {
        if (length < 2U)
        {
            return false;
        }
        uint16_t const total = (uint16_t) (((uint16_t) (data[0] & 0x0FU) << 8U) | data[1]);
        if ((total <= 7U) || (total > AUTOSAR_CAN_TP_MAX_PAYLOAD))
        {
            return false;
        }
        memcpy(tp->buffer, &data[2], (length > 8U) ? 6U : (size_t) (length - 2U));
        tp->expected_length = total;
        tp->received_length = (length >= 8U) ? 6U : (uint16_t) (length - 2U);
        tp->next_sequence = 1U;
        tp->receiving = true;
        uint8_t flow_control[8] = {0x30U, 0x00U, 0x00U, 0U, 0U, 0U, 0U, 0U};
        (void) autosar_can_tp_send_frame(tp, flow_control, 8U);
        if (tp->received_length >= tp->expected_length)
        {
            autosar_can_tp_complete(tp);
        }
        return true;
    }

    if ((pci == 2U) && tp->receiving)
    {
        uint8_t const sequence = (uint8_t) (data[0] & 0x0FU);
        if (sequence != tp->next_sequence)
        {
            tp->receiving = false;
            return false;
        }
        uint16_t const remaining = tp->expected_length - tp->received_length;
        uint8_t const available = (length > 1U) ? (uint8_t) (length - 1U) : 0U;
        uint8_t const copy_length = (remaining < available) ? (uint8_t) remaining : available;
        memcpy(&tp->buffer[tp->received_length], &data[1], copy_length);
        tp->received_length = (uint16_t) (tp->received_length + copy_length);
        tp->next_sequence = (uint8_t) ((tp->next_sequence + 1U) & 0x0FU);
        if (tp->next_sequence == 0U)
        {
            tp->next_sequence = 1U;
        }
        if (tp->received_length >= tp->expected_length)
        {
            autosar_can_tp_complete(tp);
        }
        return true;
    }
    return false;
}

void autosar_can_tp_main_function(autosar_can_tp_t * tp, uint32_t now_ms)
{
    if ((NULL != tp) && tp->receiving && ((now_ms - tp->last_rx_ms) > AUTOSAR_CAN_TP_TIMEOUT_MS))
    {
        tp->receiving = false;
        tp->expected_length = 0U;
        tp->received_length = 0U;
    }
}
