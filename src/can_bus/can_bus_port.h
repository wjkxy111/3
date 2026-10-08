#ifndef CAN_BUS_PORT_H
#define CAN_BUS_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_api.h"
#include "r_can_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_BUS_PORT_RX_QUEUE_CAPACITY    (256U)
#define CAN_BUS_PORT_MAX_DATA_BYTES       (64U)

/**
 * One frame copied out of the CAN ISR.
 *
 * FSP 6.4 names can_frame_t::data_length_code like a raw DLC code, but the
 * r_canfd driver actually returns the decoded payload length in bytes
 * (0..8, 12, 16, 20, 24, 32, 48 or 64).  data_length makes that contract
 * explicit and always mirrors frame.data_length_code.
 */
typedef struct st_bus_port_frame
{
    uint32_t    timestamp_us;
    uint8_t     data_length;
    can_frame_t frame;
} bus_port_frame_t;

/** Called from the CAN ISR, so the provider must be bounded and ISR-safe. */
typedef uint32_t (* can_bus_port_timestamp_provider_t)(void);

typedef struct st_can_bus_port_stats
{
    uint32_t rx_callback_frames;
    uint32_t rx_enqueued_frames;
    uint32_t rx_queue_dropped_frames;
    uint32_t hardware_message_lost_events;
    uint32_t tx_completed_frames;
    uint32_t tx_aborted_frames;
    uint32_t error_warning_events;
    uint32_t error_passive_events;
    uint32_t channel_error_events;
    uint32_t global_error_events;
    uint32_t bus_off_entries;
    uint32_t bus_off_recoveries;
    uint32_t bus_lock_events;
    uint32_t last_event;
    uint32_t last_error;
} can_bus_port_stats_t;

typedef struct st_can_bus_port_health
{
    can_info_t           controller;
    can_bus_port_stats_t stats;
    uint32_t             queue_depth;
    bool                 opened;
    bool                 internal_loopback;
    bool                 bus_off;
} can_bus_port_health_t;

/** Reset software state and open the FSP-generated g_canfd1 instance. */
fsp_err_t can_bus_port_init(void);

/** Close g_canfd1. Software statistics remain available after closing. */
fsp_err_t can_bus_port_deinit(void);

/**
 * Select the microsecond timestamp source used by the ISR.
 * Passing NULL restores the DWT-based fallback.  app/timebase_now_us32 is a
 * suitable provider after timebase_init() has succeeded.
 */
void can_bus_port_set_timestamp_provider(can_bus_port_timestamp_provider_t provider);

/** Pop one frame in the main loop. Returns false when the queue is empty. */
bool can_bus_port_pop(bus_port_frame_t * frame);

uint32_t can_bus_port_pending(void);
void     can_bus_port_clear(void);

/** Snapshot ISR counters and call R_CANFD_InfoGet for controller health. */
fsp_err_t can_bus_port_get_health(can_bus_port_health_t * health);

/** Enable/disable the controller's internal loopback test mode. */
fsp_err_t can_bus_port_set_internal_loopback(bool enable);

/**
 * Send a data frame through TX message buffer 0.
 * data_length is a byte count.  CAN-FD accepts 0..8/12/16/20/24/32/48/64.
 */
fsp_err_t can_bus_port_send(uint32_t            id,
                            can_id_mode_t       id_mode,
                            bool                fd_frame,
                            bool                bit_rate_switch,
                            uint8_t const      * data,
                            uint8_t              data_length);

/** Explicit, recognisable traffic generators for bench/loopback testing. */
fsp_err_t can_bus_port_send_classic_test(uint32_t sequence);
fsp_err_t can_bus_port_send_fd_test(uint32_t sequence);

/** FSP-generated callback name configured for g_canfd1. */
void canfd1_callback(can_callback_args_t * p_args);

#ifdef __cplusplus
}
#endif

#endif /* CAN_BUS_PORT_H */
