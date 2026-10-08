#ifndef AUTOSAR_CAN_TP_H
#define AUTOSAR_CAN_TP_H

#include <stdbool.h>
#include <stdint.h>

#include "autosar_types.h"

#define AUTOSAR_CAN_TP_MAX_PAYLOAD       (128U)
#define AUTOSAR_CAN_TP_TIMEOUT_MS         (1000U)

typedef bool (* autosar_can_tp_tx_fn)(uint32_t can_id,
                                      uint8_t const * data,
                                      uint8_t length,
                                      void * context);
typedef void (* autosar_can_tp_rx_fn)(uint8_t const * data,
                                      uint16_t length,
                                      void * context);

typedef struct st_autosar_can_tp
{
    uint32_t rx_id;
    uint32_t tx_id;
    uint8_t buffer[AUTOSAR_CAN_TP_MAX_PAYLOAD];
    uint16_t expected_length;
    uint16_t received_length;
    uint32_t last_rx_ms;
    uint8_t next_sequence;
    bool receiving;
    autosar_can_tp_tx_fn tx;
    autosar_can_tp_rx_fn rx;
    void * context;
} autosar_can_tp_t;

void autosar_can_tp_init(autosar_can_tp_t * tp,
                         uint32_t rx_id,
                         uint32_t tx_id,
                         autosar_can_tp_tx_fn tx,
                         autosar_can_tp_rx_fn rx,
                         void * context);
bool autosar_can_tp_on_can_frame(autosar_can_tp_t * tp,
                                 uint32_t can_id,
                                 uint8_t const * data,
                                 uint8_t length,
                                 uint32_t now_ms);
bool autosar_can_tp_send(autosar_can_tp_t * tp, uint8_t const * data, uint16_t length);
void autosar_can_tp_main_function(autosar_can_tp_t * tp, uint32_t now_ms);

#endif /* AUTOSAR_CAN_TP_H */
