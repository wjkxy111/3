#include "can_bus_port.h"

#include <stddef.h>
#include <string.h>

#include "hal_data.h"

#if ((CAN_BUS_PORT_RX_QUEUE_CAPACITY == 0U) || \
     ((CAN_BUS_PORT_RX_QUEUE_CAPACITY & (CAN_BUS_PORT_RX_QUEUE_CAPACITY - 1U)) != 0U))
 #error "CAN_BUS_PORT_RX_QUEUE_CAPACITY must be a non-zero power of two"
#endif

#define CAN_BUS_PORT_RX_QUEUE_MASK    (CAN_BUS_PORT_RX_QUEUE_CAPACITY - 1U)

/*
 * Rule 0 accepts every standard data frame and rule 1 accepts every extended
 * data frame.  Both routes feed RX FIFO 0.  Remote frames are intentionally
 * excluded because the edge detector models payload-bearing traffic.
 */
const canfd_afl_entry_t g_canfd1_afl[CANFD_CFG_AFL_CH1_RULE_NUM] =
{
    {
        .id =
        {
            .id         = 0U,
            .frame_type = CAN_FRAME_TYPE_DATA,
            .id_mode    = CAN_ID_MODE_STANDARD,
        },
        .mask =
        {
            .mask_id         = 0U,
            .mask_frame_type = 1U,
            .mask_id_mode    = 1U,
        },
        .destination =
        {
            .minimum_dlc      = CANFD_MINIMUM_DLC_0,
            .rx_buffer        = CANFD_RX_MB_NONE,
            .fifo_select_flags = CANFD_RX_FIFO_0,
        },
    },
    {
        .id =
        {
            .id         = 0U,
            .frame_type = CAN_FRAME_TYPE_DATA,
            .id_mode    = CAN_ID_MODE_EXTENDED,
        },
        .mask =
        {
            .mask_id         = 0U,
            .mask_frame_type = 1U,
            .mask_id_mode    = 1U,
        },
        .destination =
        {
            .minimum_dlc      = CANFD_MINIMUM_DLC_0,
            .rx_buffer        = CANFD_RX_MB_NONE,
            .fifo_select_flags = CANFD_RX_FIFO_0,
        },
    },
};

static bus_port_frame_t                  g_rx_queue[CAN_BUS_PORT_RX_QUEUE_CAPACITY];
static volatile uint32_t                 g_rx_head;
static volatile uint32_t                 g_rx_tail;
static volatile can_bus_port_stats_t     g_stats;
static volatile bool                     g_opened;
static volatile bool                     g_internal_loopback;
static volatile bool                     g_bus_off;
static uint32_t                          g_dwt_cycles_per_us;

static uint32_t can_bus_port_dwt_timestamp_us(void);

static can_bus_port_timestamp_provider_t g_timestamp_provider = can_bus_port_dwt_timestamp_us;

static uint32_t irq_lock(void)
{
    uint32_t const primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void irq_unlock(uint32_t primask)
{
    __DMB();
    if (0U == primask)
    {
        __enable_irq();
    }
}

static void can_bus_port_dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_dwt_cycles_per_us = SystemCoreClock / 1000000U;
}

static uint32_t can_bus_port_dwt_timestamp_us(void)
{
    uint32_t const cycles_per_us = g_dwt_cycles_per_us;
    if (0U == cycles_per_us)
    {
        return 0U;
    }
    return DWT->CYCCNT / cycles_per_us;
}

static void can_bus_port_stats_reset(void)
{
    uint32_t const primask = irq_lock();
    memset((void *) &g_stats, 0, sizeof(g_stats));
    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_internal_loopback = false;
    g_bus_off = false;
    irq_unlock(primask);
}

static void can_bus_port_stats_snapshot(can_bus_port_stats_t * destination)
{
#define CAN_BUS_PORT_COPY_STAT(member)    (destination->member = g_stats.member)
    CAN_BUS_PORT_COPY_STAT(rx_callback_frames);
    CAN_BUS_PORT_COPY_STAT(rx_enqueued_frames);
    CAN_BUS_PORT_COPY_STAT(rx_queue_dropped_frames);
    CAN_BUS_PORT_COPY_STAT(hardware_message_lost_events);
    CAN_BUS_PORT_COPY_STAT(tx_completed_frames);
    CAN_BUS_PORT_COPY_STAT(tx_aborted_frames);
    CAN_BUS_PORT_COPY_STAT(error_warning_events);
    CAN_BUS_PORT_COPY_STAT(error_passive_events);
    CAN_BUS_PORT_COPY_STAT(channel_error_events);
    CAN_BUS_PORT_COPY_STAT(global_error_events);
    CAN_BUS_PORT_COPY_STAT(bus_off_entries);
    CAN_BUS_PORT_COPY_STAT(bus_off_recoveries);
    CAN_BUS_PORT_COPY_STAT(bus_lock_events);
    CAN_BUS_PORT_COPY_STAT(last_event);
    CAN_BUS_PORT_COPY_STAT(last_error);
#undef CAN_BUS_PORT_COPY_STAT
}

static bool can_bus_port_fd_length_valid(uint8_t data_length)
{
    return (data_length <= 8U) ||
           (12U == data_length) ||
           (16U == data_length) ||
           (20U == data_length) ||
           (24U == data_length) ||
           (32U == data_length) ||
           (48U == data_length) ||
           (64U == data_length);
}

fsp_err_t can_bus_port_init(void)
{
    if (g_opened)
    {
        return FSP_ERR_ALREADY_OPEN;
    }

    can_bus_port_stats_reset();
    can_bus_port_dwt_enable();

    fsp_err_t const err = R_CANFD_Open(&g_canfd1_ctrl, &g_canfd1_cfg);
    if (FSP_SUCCESS == err)
    {
        /*
         * RA8D1 exposes CANFD0 and CANFD1 as two CANFD Lite instances, but
         * FSP 6.4 generates CANFD_CFG_GLOBAL_ERROR_CH as 0 ("Both").  The
         * driver consequently enables CAN_GLERR only when channel 0 is
         * opened.  This project intentionally opens only CANFD1, so enable
         * the shared IRQ here after R_CANFD_Open has registered its control
         * block.  The FSP ISR scans every opened instance on this device.
         */
#if (BSP_FEATURE_CANFD_NUM_INSTANCES > 1) && defined(VECTOR_NUMBER_CAN_GLERR)
        if (CANFD_CFG_GLOBAL_ERROR_CH != g_canfd1_cfg.channel)
        {
            R_BSP_IrqCfgEnable(VECTOR_NUMBER_CAN_GLERR,
                               CANFD_CFG_GLOBAL_ERR_IPL,
                               &g_canfd1_ctrl);
        }
#endif
        g_opened = true;
    }
    return err;
}

fsp_err_t can_bus_port_deinit(void)
{
    if (!g_opened)
    {
        return FSP_ERR_NOT_OPEN;
    }

    fsp_err_t const err = R_CANFD_Close(&g_canfd1_ctrl);
    if (FSP_SUCCESS == err)
    {
        uint32_t const primask = irq_lock();
        g_opened = false;
        g_internal_loopback = false;
        g_bus_off = false;
        g_rx_tail = g_rx_head;
        irq_unlock(primask);
    }
    return err;
}

void can_bus_port_set_timestamp_provider(can_bus_port_timestamp_provider_t provider)
{
    uint32_t const primask = irq_lock();
    g_timestamp_provider = (NULL != provider) ? provider : can_bus_port_dwt_timestamp_us;
    irq_unlock(primask);
}

bool can_bus_port_pop(bus_port_frame_t * frame)
{
    if (NULL == frame)
    {
        return false;
    }

    uint32_t const tail = g_rx_tail;
    uint32_t const head = g_rx_head;
    if (tail == head)
    {
        return false;
    }

    __DMB();
    *frame = g_rx_queue[tail & CAN_BUS_PORT_RX_QUEUE_MASK];
    __DMB();
    g_rx_tail = tail + 1U;
    return true;
}

uint32_t can_bus_port_pending(void)
{
    uint32_t const tail = g_rx_tail;
    __DMB();
    uint32_t depth = g_rx_head - tail;
    if (depth > CAN_BUS_PORT_RX_QUEUE_CAPACITY)
    {
        depth = CAN_BUS_PORT_RX_QUEUE_CAPACITY;
    }
    return depth;
}

void can_bus_port_clear(void)
{
    uint32_t const primask = irq_lock();
    g_rx_tail = g_rx_head;
    irq_unlock(primask);
}

fsp_err_t can_bus_port_get_health(can_bus_port_health_t * health)
{
    if (NULL == health)
    {
        return FSP_ERR_ASSERTION;
    }

    memset(health, 0, sizeof(*health));

    uint32_t const primask = irq_lock();
    can_bus_port_stats_snapshot(&health->stats);
    health->queue_depth = g_rx_head - g_rx_tail;
    if (health->queue_depth > CAN_BUS_PORT_RX_QUEUE_CAPACITY)
    {
        health->queue_depth = CAN_BUS_PORT_RX_QUEUE_CAPACITY;
    }
    health->opened = g_opened;
    health->internal_loopback = g_internal_loopback;
    health->bus_off = g_bus_off;
    irq_unlock(primask);

    if (!health->opened)
    {
        return FSP_ERR_NOT_OPEN;
    }

    return R_CANFD_InfoGet(&g_canfd1_ctrl, &health->controller);
}

fsp_err_t can_bus_port_set_internal_loopback(bool enable)
{
    if (!g_opened)
    {
        return FSP_ERR_NOT_OPEN;
    }

    can_test_mode_t const test_mode = enable ? CAN_TEST_MODE_LOOPBACK_INTERNAL : CAN_TEST_MODE_DISABLED;
    fsp_err_t const err = R_CANFD_ModeTransition(&g_canfd1_ctrl, CAN_OPERATION_MODE_NORMAL, test_mode);
    if (FSP_SUCCESS == err)
    {
        g_internal_loopback = enable;
        if (enable)
        {
            g_bus_off = false;
        }
    }
    return err;
}

fsp_err_t can_bus_port_send(uint32_t       id,
                            can_id_mode_t  id_mode,
                            bool           fd_frame,
                            bool           bit_rate_switch,
                            uint8_t const * data,
                            uint8_t         data_length)
{
    if (!g_opened)
    {
        return FSP_ERR_NOT_OPEN;
    }
    if (((CAN_ID_MODE_STANDARD != id_mode) && (CAN_ID_MODE_EXTENDED != id_mode)) ||
        ((CAN_ID_MODE_STANDARD == id_mode) && (id > 0x7FFU)) ||
        ((CAN_ID_MODE_EXTENDED == id_mode) && (id > 0x1FFFFFFFU)) ||
        ((data_length > 0U) && (NULL == data)) ||
        ((!fd_frame) && ((data_length > 8U) || bit_rate_switch)) ||
        (fd_frame && !can_bus_port_fd_length_valid(data_length)))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    can_frame_t frame = {0U};
    frame.id = id;
    frame.id_mode = id_mode;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.data_length_code = data_length; /* FSP 6.4 expects bytes, not the encoded DLC nibble. */
    frame.options = fd_frame ? CANFD_FRAME_OPTION_FD : 0U;
    if (fd_frame && bit_rate_switch)
    {
        frame.options |= CANFD_FRAME_OPTION_BRS;
    }
    if (data_length > 0U)
    {
        memcpy(frame.data, data, data_length);
    }

    return R_CANFD_Write(&g_canfd1_ctrl, (uint32_t) CANFD_TX_BUFFER_0, &frame);
}

fsp_err_t can_bus_port_send_classic_test(uint32_t sequence)
{
    uint8_t const payload[8] =
    {
        0x43U, 0x41U, 0x4EU, 0x31U,
        (uint8_t) sequence,
        (uint8_t) (sequence >> 8),
        (uint8_t) (sequence >> 16),
        (uint8_t) (sequence >> 24),
    };
    return can_bus_port_send(0x321U,
                             CAN_ID_MODE_STANDARD,
                             false,
                             false,
                             payload,
                             (uint8_t) sizeof(payload));
}

fsp_err_t can_bus_port_send_fd_test(uint32_t sequence)
{
    uint8_t const payload[16] =
    {
        0x43U, 0x41U, 0x4EU, 0x46U, 0x44U, 0x2DU, 0x41U, 0x49U,
        (uint8_t) sequence,
        (uint8_t) (sequence >> 8),
        (uint8_t) (sequence >> 16),
        (uint8_t) (sequence >> 24),
        0x50U, 0x52U, 0x4FU, 0x42U,
    };
    return can_bus_port_send(0x18FF50E5U,
                             CAN_ID_MODE_EXTENDED,
                             true,
                             true,
                             payload,
                             (uint8_t) sizeof(payload));
}

void canfd1_callback(can_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    uint32_t const event = (uint32_t) p_args->event;

    /*
     * FSP's RX/TX paths do not initialize callback_args.error.  It is valid
     * only for channel/global error callbacks; reading it for a normal frame
     * can otherwise turn stack garbage into a false Bus-Off or recovery.
     */
    uint32_t const error = (0U != (event & ((uint32_t) CAN_EVENT_ERR_CHANNEL |
                                            (uint32_t) CAN_EVENT_ERR_GLOBAL))) ?
                           p_args->error : 0U;
    g_stats.last_event = event;
    if (0U != error)
    {
        g_stats.last_error = error;
    }

    if (0U != (event & (uint32_t) CAN_EVENT_RX_COMPLETE))
    {
        g_stats.rx_callback_frames++;

        uint32_t const head = g_rx_head;
        uint32_t const tail = g_rx_tail;
        if ((head - tail) >= CAN_BUS_PORT_RX_QUEUE_CAPACITY)
        {
            g_stats.rx_queue_dropped_frames++;
        }
        else
        {
            can_bus_port_timestamp_provider_t const timestamp_provider = g_timestamp_provider;
            bus_port_frame_t * const destination = &g_rx_queue[head & CAN_BUS_PORT_RX_QUEUE_MASK];

            destination->timestamp_us = (NULL != timestamp_provider) ? timestamp_provider() : 0U;
            destination->data_length = p_args->frame.data_length_code;
            destination->frame = p_args->frame; /* Deep copy ID, flags and the complete 64-byte data array. */

            __DMB();
            g_rx_head = head + 1U;
            g_stats.rx_enqueued_frames++;
        }
    }

    if (0U != (event & (uint32_t) CAN_EVENT_TX_COMPLETE))
    {
        g_stats.tx_completed_frames++;
    }
    if (0U != (event & (uint32_t) CAN_EVENT_TX_ABORTED))
    {
        g_stats.tx_aborted_frames++;
    }
    if (0U != (event & (uint32_t) CAN_EVENT_ERR_CHANNEL))
    {
        g_stats.channel_error_events++;
    }
    if (0U != (event & (uint32_t) CAN_EVENT_ERR_GLOBAL))
    {
        g_stats.global_error_events++;
    }

    if ((0U != (event & (uint32_t) CAN_EVENT_ERR_WARNING)) ||
        (0U != (error & (uint32_t) CANFD_ERROR_CHANNEL_WARNING)))
    {
        g_stats.error_warning_events++;
    }
    if ((0U != (event & (uint32_t) CAN_EVENT_ERR_PASSIVE)) ||
        (0U != (error & (uint32_t) CANFD_ERROR_CHANNEL_PASSIVE)))
    {
        g_stats.error_passive_events++;
    }
    if ((0U != (event & (uint32_t) CAN_EVENT_ERR_BUS_OFF)) ||
        (0U != (error & (uint32_t) CANFD_ERROR_CHANNEL_BUS_OFF_ENTRY)))
    {
        g_stats.bus_off_entries++;
        g_bus_off = true;
    }
    if ((0U != (event & (uint32_t) CAN_EVENT_BUS_RECOVERY)) ||
        (0U != (error & (uint32_t) CANFD_ERROR_CHANNEL_BUS_OFF_RECOVERY)))
    {
        g_stats.bus_off_recoveries++;
        g_bus_off = false;
    }
    if ((0U != (event & (uint32_t) CAN_EVENT_ERR_BUS_LOCK)) ||
        (0U != (error & (uint32_t) CANFD_ERROR_CHANNEL_BUS_LOCK)))
    {
        g_stats.bus_lock_events++;
    }
    if ((0U != (event & ((uint32_t) CAN_EVENT_MAILBOX_MESSAGE_LOST |
                         (uint32_t) CAN_EVENT_FIFO_MESSAGE_LOST))) ||
        (0U != (error & (uint32_t) CANFD_ERROR_GLOBAL_MESSAGE_LOST)))
    {
        g_stats.hardware_message_lost_events++;
    }
}
