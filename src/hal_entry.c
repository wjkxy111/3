#include "hal_data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app/can_event_log.h"
#include "app/can_self_test.h"
#include "app/console.h"
#include "app/autosar_stack.h"
#include "app/timebase.h"
#include "can_ai/can_anomaly.h"
#include "can_bus/can_bus_port.h"
#include "r_canfd.h"
#include "ui/can_ui.h"

#define APP_LED_PIN                    BSP_IO_PORT_06_PIN_00
#define APP_KEY_PIN                    BSP_IO_PORT_00_PIN_06
#define APP_CAN_STANDBY_PIN            BSP_IO_PORT_00_PIN_05
#define APP_LED_ALARM_LEVEL            BSP_IO_LEVEL_LOW
#define APP_LED_IDLE_LEVEL             BSP_IO_LEVEL_HIGH

#define APP_KEY_DEBOUNCE_US            (30000ULL)
#define APP_KEY_LONG_PRESS_US          (800000ULL)
#define APP_REAL_LEARNING_US           (10000000ULL)
#define APP_REAL_LEARNING_MIN_FRAMES   (100ULL)
#define APP_EVENT_COOLDOWN_US          (200000ULL)
#define APP_ALARM_HOLD_US              (1000000ULL)
#define APP_UI_PERIOD_US               (200000ULL)
#define APP_HEALTH_PERIOD_US           (1000000ULL)
#define APP_METRICS_PERIOD_US          (1000000ULL)
#define APP_MAX_RX_PER_LOOP            (128U)
#define APP_MAX_SYNTHETIC_PER_LOOP     (32U)

typedef struct st_app_live_frame
{
    uint64_t timestamp_us;
    uint64_t sequence;
    uint32_t id;
    uint32_t reason_mask;
    uint16_t score;
    uint8_t  data_length;
    uint8_t  flags;
    uint8_t  data[CAN_AI_MAX_DATA_BYTES];
} app_live_frame_t;

static can_ai_detector_t      g_detector;
static can_self_test_t        g_self_test;
static can_event_log_t        g_event_log;
static ui_view_model_t        g_ui_model;
static app_live_frame_t       g_live_frames[CAN_UI_MAX_LIVE_FRAMES];
static can_bus_port_health_t  g_bus_health;

static uint8_t  g_live_frame_count;
static bool     g_bus_ready;
static bool     g_display_ready;
static bool     g_health_valid;
static bool     g_internal_loopback;
static uint64_t g_last_ui_us;
static uint64_t g_last_health_us;
static uint64_t g_metrics_started_us;
static uint64_t g_metrics_busy_ns;
static uint64_t g_total_processed_frames;
static uint32_t g_metrics_frames;
static uint32_t g_frames_per_second;
static uint16_t g_bus_load_permille;
static uint32_t g_invalid_frames;
static uint32_t g_current_reason_mask;
static uint16_t g_current_score;
static uint64_t g_last_anomaly_us;
static bool     g_event_signature_valid;
static uint32_t g_last_event_id;
static uint32_t g_last_event_reason;
static uint64_t g_last_event_us;
static uint32_t g_tx_sequence;
static uint64_t g_live_sequence;
static uint64_t g_last_host_frame_sequence;

static uint32_t app_u64_to_u32(uint64_t value)
{
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t) value;
}

static uint16_t app_u32_to_u16(uint32_t value)
{
    return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t) value;
}

static uint32_t app_reason_to_ui(uint32_t reason)
{
    uint32_t mapped = UI_ANOMALY_NONE;

    if (0U != (reason & CAN_AI_REASON_UNKNOWN_ID))
    {
        mapped |= UI_ANOMALY_UNKNOWN_ID;
    }
    if (0U != (reason & CAN_AI_REASON_DLC_CHANGED))
    {
        mapped |= UI_ANOMALY_DLC;
    }
    if (0U != (reason & (CAN_AI_REASON_PERIOD_FAST | CAN_AI_REASON_PERIOD_SLOW)))
    {
        mapped |= UI_ANOMALY_TIMING;
    }
    if (0U != (reason & CAN_AI_REASON_PAYLOAD_JUMP))
    {
        mapped |= UI_ANOMALY_PAYLOAD;
    }
    if (0U != (reason & CAN_AI_REASON_GLOBAL_FLOOD))
    {
        mapped |= UI_ANOMALY_FLOOD;
    }

    return mapped;
}

static uint8_t app_frame_flags(bool extended, bool fd_frame, bool brs, bool remote)
{
    uint8_t flags = UI_FRAME_FLAG_NONE;

    if (extended)
    {
        flags |= UI_FRAME_FLAG_EXTENDED;
    }
    if (fd_frame)
    {
        flags |= UI_FRAME_FLAG_FD;
    }
    if (brs)
    {
        flags |= UI_FRAME_FLAG_BRS;
    }
    if (remote)
    {
        flags |= UI_FRAME_FLAG_RTR;
    }
    return flags;
}

static uint64_t app_extend_timestamp(uint32_t timestamp_us, uint64_t now_us)
{
    uint64_t candidate = (now_us & 0xFFFFFFFF00000000ULL) | timestamp_us;

    if ((candidate > now_us) && ((candidate - now_us) > 0x80000000ULL))
    {
        candidate -= 0x100000000ULL;
    }
    else if ((now_us > candidate) && ((now_us - candidate) > 0x80000000ULL))
    {
        candidate += 0x100000000ULL;
    }
    return candidate;
}

static uint64_t app_estimated_frame_time_ns(uint8_t flags, uint8_t data_length)
{
    bool const extended = (0U != (flags & UI_FRAME_FLAG_EXTENDED));
    bool const fd_frame = (0U != (flags & UI_FRAME_FLAG_FD));

    if (!fd_frame)
    {
        uint32_t bits = (extended ? 67U : 47U) + ((uint32_t) data_length * 8U);
        bits = ((bits * 6U) + 4U) / 5U;
        return (uint64_t) bits * 2000ULL; /* 500 kbit/s nominal phase. */
    }

    uint32_t nominal_bits = extended ? 54U : 34U;
    uint32_t data_bits = ((uint32_t) data_length * 8U) + ((data_length > 16U) ? 33U : 28U);
    uint64_t const data_bit_time_ns = (0U != (flags & UI_FRAME_FLAG_BRS)) ? 500ULL : 2000ULL;
    nominal_bits = ((nominal_bits * 6U) + 4U) / 5U;
    data_bits = ((data_bits * 6U) + 4U) / 5U;
    return ((uint64_t) nominal_bits * 2000ULL) + ((uint64_t) data_bits * data_bit_time_ns);
}

static void app_update_live_frame(uint32_t       id,
                                  uint8_t        data_length,
                                  uint8_t        flags,
                                  uint8_t const *data,
                                  uint32_t       reason_mask,
                                  uint16_t       score,
                                  uint64_t       timestamp_us)
{
    uint8_t last = g_live_frame_count;
    if (last >= CAN_UI_MAX_LIVE_FRAMES)
    {
        last = CAN_UI_MAX_LIVE_FRAMES - 1U;
    }
    else
    {
        g_live_frame_count++;
    }

    for (uint8_t index = last; index > 0U; index--)
    {
        g_live_frames[index] = g_live_frames[index - 1U];
    }

    app_live_frame_t * const live = &g_live_frames[0];
    memset(live, 0, sizeof(*live));
    live->timestamp_us = timestamp_us;
    live->sequence = ++g_live_sequence;
    live->id = id;
    live->reason_mask = reason_mask;
    live->score = score;
    live->data_length = data_length;
    live->flags = flags;

    uint8_t copy_length = data_length;
    if (copy_length > CAN_AI_MAX_DATA_BYTES)
    {
        copy_length = CAN_AI_MAX_DATA_BYTES;
    }
    if ((copy_length > 0U) && (NULL != data))
    {
        memcpy(live->data, data, copy_length);
    }
}

static void app_record_event(uint32_t id,
                             uint8_t  data_length,
                             uint8_t  flags,
                             uint32_t reason_mask,
                             uint16_t score,
                             uint64_t timestamp_us)
{
    bool const duplicate = g_event_signature_valid &&
                           (g_last_event_id == id) &&
                           (g_last_event_reason == reason_mask) &&
                           ((timestamp_us - g_last_event_us) < APP_EVENT_COOLDOWN_US);
    if (duplicate)
    {
        return;
    }

    can_event_record_t event = {0};
    event.timestamp_us = timestamp_us;
    event.can_id = id;
    event.reason_mask = reason_mask;
    event.score = score;
    event.data_length = data_length;
    event.flags = flags;
    can_event_log_push(&g_event_log, &event);

    g_event_signature_valid = true;
    g_last_event_id = id;
    g_last_event_reason = reason_mask;
    g_last_event_us = timestamp_us;

    (void) console_printf("EVENT,T+%lu,ID,0x%08lX,DLC,%u,SCORE,%u,REASON,0x%02lX\r\n",
                          (unsigned long) app_u64_to_u32(timestamp_us / 1000ULL),
                          (unsigned long) id,
                          (unsigned int) data_length,
                          (unsigned int) score,
                          (unsigned long) reason_mask);
}

static void app_process_frame(uint32_t         id,
                              can_ai_id_mode_t id_mode,
                              uint8_t          data_length,
                              uint8_t          flags,
                              uint8_t const   *data,
                              uint64_t         timestamp_us)
{
    uint8_t const dlc = can_ai_length_to_dlc(data_length);
    if ((0xFFU == dlc) || ((data_length > 0U) && (NULL == data)))
    {
        g_invalid_frames++;
        return;
    }

    can_ai_frame_t frame =
    {
        .id = id,
        .id_mode = id_mode,
        .dlc = dlc,
        .data = data,
        .timestamp_us = timestamp_us,
    };
    can_ai_result_t result;
    can_ai_status_t const status = can_ai_process(&g_detector, &frame, &result);

    g_total_processed_frames++;
    g_metrics_frames++;
    g_metrics_busy_ns += app_estimated_frame_time_ns(flags, data_length);

    uint32_t reason_mask = app_reason_to_ui(result.reasons);
    uint16_t score = result.score;
    if (CAN_AI_STATUS_PROFILE_LIMIT == status)
    {
        reason_mask |= UI_ANOMALY_MODEL_FULL;
        if (score < 500U)
        {
            score = 500U;
        }
    }
    else if (CAN_AI_STATUS_OK != status)
    {
        g_invalid_frames++;
        return;
    }

    app_update_live_frame(id, data_length, flags, data, reason_mask, score, timestamp_us);
    if (UI_ANOMALY_NONE != reason_mask)
    {
        g_current_reason_mask = reason_mask;
        g_current_score = score;
        g_last_anomaly_us = timestamp_us;
        app_record_event(id, data_length, flags, reason_mask, score, timestamp_us);
    }
}

static void app_restart_learning(uint64_t now_us)
{
    can_ai_start_learning(&g_detector);
    can_event_log_init(&g_event_log);
    memset(g_live_frames, 0, sizeof(g_live_frames));
    g_live_frame_count = 0U;
    g_current_reason_mask = UI_ANOMALY_NONE;
    g_current_score = 0U;
    g_last_anomaly_us = now_us;
    g_event_signature_valid = false;

    if (can_self_test_enabled(&g_self_test))
    {
        can_self_test_set_enabled(&g_self_test, true, (uint32_t) now_us);
    }
    (void) console_write("MODEL,LEARNING,profiles_cleared\r\n");
}

static bool app_finish_learning(void)
{
    if (!can_ai_finish_learning(&g_detector))
    {
        (void) console_write("MODEL,WAITING,no_CAN_frames\r\n");
        return false;
    }

    can_ai_stats_t stats;
    can_ai_get_stats(&g_detector, &stats);
    (void) console_printf("MODEL,MONITORING,IDS,%u,BASE_FPS,%lu,FLOOD_FPS,%lu\r\n",
                          (unsigned int) stats.profile_count,
                          (unsigned long) stats.learned_rate_fps,
                          (unsigned long) stats.flood_threshold_fps);
    return true;
}

static uint32_t app_bus_error_count(void)
{
    if (!g_health_valid)
    {
        return g_bus_ready ? 0U : 1U;
    }

    can_bus_port_stats_t const * const stats = &g_bus_health.stats;
    return stats->hardware_message_lost_events +
           stats->tx_aborted_frames +
           stats->error_warning_events +
           stats->error_passive_events +
           stats->channel_error_events +
           stats->global_error_events +
           stats->bus_off_entries +
           stats->bus_lock_events;
}

static bool app_bus_problem(void)
{
    if (can_self_test_enabled(&g_self_test))
    {
        return false;
    }
    if (!g_bus_ready)
    {
        return true;
    }
    return g_health_valid &&
           (g_bus_health.bus_off || (0U != (g_bus_health.controller.status & CANFD_STATUS_BUS_OFF)));
}

static void app_update_metrics(uint64_t now_us)
{
    uint64_t const elapsed_us = now_us - g_metrics_started_us;
    if (elapsed_us < APP_METRICS_PERIOD_US)
    {
        return;
    }

    uint64_t fps = ((uint64_t) g_metrics_frames * 1000000ULL) / elapsed_us;
    uint64_t load_permille = g_metrics_busy_ns / elapsed_us;
    g_frames_per_second = app_u64_to_u32(fps);
    if (load_permille > 1000ULL)
    {
        load_permille = 1000ULL;
    }
    g_bus_load_permille = (uint16_t) load_permille;
    g_metrics_frames = 0U;
    g_metrics_busy_ns = 0U;
    g_metrics_started_us = now_us;
}

static uint32_t app_float_to_u32(float value)
{
    if (value <= 0.0F)
    {
        return 0U;
    }
    if (value >= (float) UINT32_MAX)
    {
        return UINT32_MAX;
    }
    return (uint32_t) (value + 0.5F);
}

static void app_refresh_ui(uint64_t now_us)
{
    can_ai_stats_t ai_stats;
    can_ai_profile_view_t profiles[CAN_UI_MAX_PROFILES];
    can_event_record_t events[CAN_UI_MAX_EVENTS];
    can_ai_get_stats(&g_detector, &ai_stats);

    memset(&g_ui_model, 0, sizeof(g_ui_model));
    g_ui_model.uptime_ms = app_u64_to_u32(now_us / 1000ULL);
    g_ui_model.total_frames = app_u64_to_u32(g_total_processed_frames);
    g_ui_model.anomaly_frames = app_u64_to_u32(ai_stats.anomaly_frames);
    g_ui_model.unknown_frames = ai_stats.unknown_frames;
    g_ui_model.nominal_bitrate_kbps = 500U;
    g_ui_model.data_bitrate_kbps = 2000U;
    g_ui_model.can_fd_enabled = true;
    g_ui_model.frames_per_second = app_u32_to_u16(g_frames_per_second);
    g_ui_model.bus_load_permille = g_bus_load_permille;
    g_ui_model.current_reason_mask = g_current_reason_mask;
    g_ui_model.anomaly_score = g_current_score;
    g_ui_model.learned_id_count = ai_stats.profile_count;
    g_ui_model.demo_enabled = can_self_test_enabled(&g_self_test);
    g_ui_model.loopback_enabled = g_internal_loopback;
    g_ui_model.health_valid = g_health_valid;
    g_ui_model.bus_error_count = app_u32_to_u16(app_bus_error_count());
    g_ui_model.display_error_code = g_display_ready ? 0U : 1U;

    if (g_health_valid)
    {
        g_ui_model.tec = g_bus_health.controller.error_count_transmit;
        g_ui_model.rec = g_bus_health.controller.error_count_receive;
        g_ui_model.dropped_frames = g_bus_health.stats.rx_queue_dropped_frames +
                                    g_bus_health.stats.hardware_message_lost_events +
                                    g_invalid_frames;
        g_ui_model.bus_off_count = g_bus_health.stats.bus_off_entries;
    }
    else
    {
        g_ui_model.dropped_frames = g_invalid_frames;
    }

    if (!g_display_ready)
    {
        g_ui_model.state = UI_SYSTEM_STATE_DISPLAY_ERROR;
    }
    else if (app_bus_problem())
    {
        g_ui_model.state = UI_SYSTEM_STATE_BUS_ERROR;
        g_ui_model.current_reason_mask |= UI_ANOMALY_BUS;
        if (g_ui_model.anomaly_score < 700U)
        {
            g_ui_model.anomaly_score = 700U;
        }
    }
    else
    {
        g_ui_model.state = (CAN_AI_MODE_LEARNING == ai_stats.mode) ?
                           UI_SYSTEM_STATE_LEARNING : UI_SYSTEM_STATE_MONITORING;
    }

    size_t const profile_count = can_ai_get_top_profiles(&g_detector,
                                                         profiles,
                                                         CAN_UI_MAX_PROFILES);
    g_ui_model.profile_count = (uint8_t) profile_count;
    for (size_t index = 0U; index < profile_count; index++)
    {
        ui_id_profile_t * const output = &g_ui_model.profiles[index];
        can_ai_profile_view_t const * const input = &profiles[index];
        output->id = input->id;
        output->observed_frames = app_u64_to_u32(input->observed_frames);
        output->period_us = app_float_to_u32(input->mean_period_us);
        output->anomaly_score = input->last_score;
        output->dlc = input->expected_length;
        output->flags = (CAN_AI_ID_EXTENDED == input->id_mode) ? UI_FRAME_FLAG_EXTENDED : 0U;
        if (input->mean_period_us > 0.0F)
        {
            float const rate_x10 = 10000000.0F / input->mean_period_us;
            output->rate_hz_x10 = (rate_x10 >= (float) UINT16_MAX) ?
                                  UINT16_MAX : (uint16_t) (rate_x10 + 0.5F);
        }
    }

    g_ui_model.live_frame_count = g_live_frame_count;
    for (uint8_t index = 0U; index < g_live_frame_count; index++)
    {
        app_live_frame_t const * const input = &g_live_frames[index];
        ui_live_frame_t * const output = &g_ui_model.live_frames[index];
        output->id = input->id;
        output->age_ms = app_u64_to_u32((now_us - input->timestamp_us) / 1000ULL);
        output->reason_mask = input->reason_mask;
        output->anomaly_score = input->score;
        output->dlc = input->data_length;
        output->flags = input->flags;
        memcpy(output->data, input->data, sizeof(output->data));
    }

    size_t const event_count = can_event_log_copy_recent(&g_event_log,
                                                         events,
                                                         CAN_UI_MAX_EVENTS);
    g_ui_model.event_count = (uint8_t) event_count;
    for (size_t index = 0U; index < event_count; index++)
    {
        g_ui_model.events[index].timestamp_ms = app_u64_to_u32(events[index].timestamp_us / 1000ULL);
        g_ui_model.events[index].id = events[index].can_id;
        g_ui_model.events[index].reason_mask = events[index].reason_mask;
        g_ui_model.events[index].anomaly_score = events[index].score;
        g_ui_model.events[index].dlc = events[index].data_length;
        g_ui_model.events[index].flags = events[index].flags;
    }

    can_ui_set_model(&g_ui_model);
    if (g_display_ready && !can_ui_render_if_dirty())
    {
        g_display_ready = false;
        (void) console_write("DISPLAY,ERROR,runtime\r\n");
    }
}

static void app_print_help(void)
{
    (void) console_write("\r\nRA8D1 CAN-FD edge IDS (CANFD1, 500K/2M)\r\n");
    (void) console_write("  l  restart normal-traffic learning\r\n");
    (void) console_write("  m  freeze model and enter monitoring\r\n");
    (void) console_write("  s  toggle offline synthetic attack demo\r\n");
    (void) console_write("  o  toggle CAN controller internal loopback\r\n");
    (void) console_write("  c  send one Classical CAN test frame\r\n");
    (void) console_write("  f  send one CAN-FD+BRS test frame\r\n");
    (void) console_write("  d  send UDS 0x10 extended-session test on 0x7E0\r\n");
    (void) console_write("  u  next display page (5 pages)\r\n");
    (void) console_write("  i  print detector/controller diagnostics\r\n");
    (void) console_write("  e  dump learned profiles and IDS events\r\n");
    (void) console_write("  q  print one machine-readable host snapshot\r\n");
    (void) console_write("  h  print help\r\n");
    (void) console_write("S1 short: next page; hold 0.8 s: relearn.\r\n\r\n");
}

static char const * app_host_health(void)
{
    if (!g_display_ready)
    {
        return "DISP_ERR";
    }
    if ((!g_bus_ready) ||
        (g_health_valid &&
         (g_bus_health.bus_off || (0U != (g_bus_health.controller.status & CANFD_STATUS_BUS_OFF)))))
    {
        return "BUS_ERR";
    }
    return g_health_valid ? "OK" : "NA";
}

static void app_print_host_frame(void)
{
    if (0U == g_live_frame_count)
    {
        return;
    }

    app_live_frame_t const * const frame = &g_live_frames[0];
    if (frame->sequence == g_last_host_frame_sequence)
    {
        return;
    }
    bool const extended = (0U != (frame->flags & UI_FRAME_FLAG_EXTENDED));
    bool const fd_frame = (0U != (frame->flags & UI_FRAME_FLAG_FD));
    bool const brs = (0U != (frame->flags & UI_FRAME_FLAG_BRS));
    bool const remote = (0U != (frame->flags & UI_FRAME_FLAG_RTR));

    if (!console_printf("FRAME,SEQ,%llu,T_MS,%lu,ID,0x%08lX,FMT,%s,FD,%s,BRS,%s,RTR,%s,DLC,%u,SCORE,%u,REASON,0x%08lX,DATA,",
                        (unsigned long long) frame->sequence,
                        (unsigned long) app_u64_to_u32(frame->timestamp_us / 1000ULL),
                        (unsigned long) frame->id,
                        extended ? "EXT" : "STD",
                        fd_frame ? "ON" : "OFF",
                        brs ? "ON" : "OFF",
                        remote ? "ON" : "OFF",
                        (unsigned int) frame->data_length,
                        (unsigned int) frame->score,
                        (unsigned long) frame->reason_mask))
    {
        return;
    }

    static char const hex[] = "0123456789ABCDEF";
    char data_hex[(CAN_AI_MAX_DATA_BYTES * 2U) + 1U];
    uint8_t data_length = remote ? 0U : frame->data_length;
    if (data_length > CAN_AI_MAX_DATA_BYTES)
    {
        data_length = CAN_AI_MAX_DATA_BYTES;
    }
    for (uint8_t index = 0U; index < data_length; index++)
    {
        data_hex[index * 2U] = hex[frame->data[index] >> 4U];
        data_hex[(index * 2U) + 1U] = hex[frame->data[index] & 0x0FU];
    }
    data_hex[data_length * 2U] = '\0';
    if (console_write(data_hex) && console_write("\r\n"))
    {
        g_last_host_frame_sequence = frame->sequence;
    }
}

static void app_print_host_snapshot(uint64_t now_us)
{
    can_ai_stats_t stats;
    can_ai_get_stats(&g_detector, &stats);

    uint8_t tec = 0U;
    uint8_t rec = 0U;
    uint32_t dropped = g_invalid_frames;
    uint32_t bus_off = 0U;
    if (g_health_valid)
    {
        tec = g_bus_health.controller.error_count_transmit;
        rec = g_bus_health.controller.error_count_receive;
        dropped += g_bus_health.stats.rx_queue_dropped_frames +
                   g_bus_health.stats.hardware_message_lost_events;
        bus_off = g_bus_health.stats.bus_off_entries;
    }

    /* The worst-case line is 243 bytes including CRLF, below console's 256-byte buffer. */
    (void) console_printf("TELEM,T_MS,%lu,MODE,%s,DEMO,%s,LOOPBACK,%s,FPS,%lu,LOAD_PM,%u,IDS,%u,SCORE,%u,REASON,0x%08lX,TOTAL,%lu,ANOMALY,%lu,UNKNOWN,%lu,HEALTH,%s,TEC,%u,REC,%u,DROP,%lu,BUSOFF,%lu\r\n",
                          (unsigned long) app_u64_to_u32(now_us / 1000ULL),
                          (CAN_AI_MODE_LEARNING == stats.mode) ? "LEARN" : "MONITOR",
                          can_self_test_enabled(&g_self_test) ? "ON" : "OFF",
                          g_internal_loopback ? "ON" : "OFF",
                          (unsigned long) g_frames_per_second,
                          (unsigned int) g_bus_load_permille,
                          (unsigned int) stats.profile_count,
                          (unsigned int) g_current_score,
                          (unsigned long) g_current_reason_mask,
                          (unsigned long) app_u64_to_u32(stats.total_frames),
                          (unsigned long) app_u64_to_u32(stats.anomaly_frames),
                          (unsigned long) stats.unknown_frames,
                          app_host_health(),
                          (unsigned int) tec,
                          (unsigned int) rec,
                          (unsigned long) dropped,
                          (unsigned long) bus_off);
    app_print_host_frame();
}

static void app_print_diagnostics(void)
{
    can_ai_stats_t stats;
    can_ai_get_stats(&g_detector, &stats);
    (void) console_printf("DIAG,MODE,%s,DEMO,%s,LOOPBACK,%s,IDS,%u,FPS,%lu,LOAD_PM,%u\r\n",
                          (CAN_AI_MODE_LEARNING == stats.mode) ? "LEARN" : "MONITOR",
                          can_self_test_enabled(&g_self_test) ? "ON" : "OFF",
                          g_internal_loopback ? "ON" : "OFF",
                          (unsigned int) stats.profile_count,
                          (unsigned long) g_frames_per_second,
                          (unsigned int) g_bus_load_permille);
    (void) console_printf("AI,TOTAL,%lu,ANOMALY,%lu,UNKNOWN,%lu,BASE_FPS,%lu,FLOOD_FPS,%lu\r\n",
                          (unsigned long) app_u64_to_u32(stats.total_frames),
                          (unsigned long) app_u64_to_u32(stats.anomaly_frames),
                          (unsigned long) stats.unknown_frames,
                          (unsigned long) stats.learned_rate_fps,
                          (unsigned long) stats.flood_threshold_fps);
    if (g_health_valid)
    {
        (void) console_printf("CAN,STATUS,0x%08lX,TEC,%u,REC,%u,RX,%lu,DROP,%lu,BUSOFF,%lu,ERR,0x%08lX\r\n",
                              (unsigned long) g_bus_health.controller.status,
                              (unsigned int) g_bus_health.controller.error_count_transmit,
                              (unsigned int) g_bus_health.controller.error_count_receive,
                              (unsigned long) g_bus_health.stats.rx_enqueued_frames,
                              (unsigned long) g_bus_health.stats.rx_queue_dropped_frames,
                              (unsigned long) g_bus_health.stats.bus_off_entries,
                              (unsigned long) g_bus_health.stats.last_error);
    }
    else
    {
        (void) console_write("CAN,HEALTH,UNAVAILABLE\r\n");
    }

    autosar_stack_status_t autosar_status;
    autosar_stack_get_status(&autosar_status);
    (void) console_printf("AUTOSAR,STACK,%s,UDS_RX,%lu,UDS_TX,%lu,SESSION,%s,BOOT_CHECK,%s\r\n",
                          autosar_status.initialized ? "READY" : "ERROR",
                          (unsigned long) autosar_status.rx_requests,
                          (unsigned long) autosar_status.tx_responses,
                          autosar_dcm_session_name(autosar_status.session),
                          autosar_boot_validation_name(autosar_status.boot_validation));
}

static void app_dump_model_and_events(void)
{
    can_ai_profile_view_t profiles[CAN_AI_MAX_PROFILES];
    size_t const profile_count = can_ai_get_top_profiles(&g_detector,
                                                         profiles,
                                                         CAN_AI_MAX_PROFILES);
    for (size_t index = 0U; index < profile_count; index++)
    {
        can_ai_profile_view_t const * const profile = &profiles[index];
        (void) console_printf("PROFILE,%u,ID,0x%08lX,%s,DLC,%u,N,%lu,PERIOD_US,%lu,RISK,%u\r\n",
                              (unsigned int) index,
                              (unsigned long) profile->id,
                              (CAN_AI_ID_EXTENDED == profile->id_mode) ? "EXT" : "STD",
                              (unsigned int) profile->expected_length,
                              (unsigned long) app_u64_to_u32(profile->observed_frames),
                              (unsigned long) app_float_to_u32(profile->mean_period_us),
                              (unsigned int) profile->last_score);
    }

    can_event_record_t events[CAN_EVENT_LOG_CAPACITY];
    size_t const event_count = can_event_log_copy_recent(&g_event_log,
                                                         events,
                                                         CAN_EVENT_LOG_CAPACITY);
    for (size_t index = 0U; index < event_count; index++)
    {
        (void) console_printf("LOG,%u,T_MS,%lu,ID,0x%08lX,DLC,%u,SCORE,%u,REASON,0x%02lX\r\n",
                              (unsigned int) index,
                              (unsigned long) app_u64_to_u32(events[index].timestamp_us / 1000ULL),
                              (unsigned long) events[index].can_id,
                              (unsigned int) events[index].data_length,
                              (unsigned int) events[index].score,
                              (unsigned long) events[index].reason_mask);
    }
}

static void app_handle_command(uint8_t command, uint64_t now_us)
{
    if ((command >= (uint8_t) 'A') && (command <= (uint8_t) 'Z'))
    {
        command = (uint8_t) (command + ((uint8_t) 'a' - (uint8_t) 'A'));
    }

    switch (command)
    {
        case (uint8_t) 'l':
            app_restart_learning(now_us);
            break;

        case (uint8_t) 'm':
            (void) app_finish_learning();
            break;

        case (uint8_t) 's':
        {
            bool const enable = !can_self_test_enabled(&g_self_test);
            can_self_test_set_enabled(&g_self_test, enable, (uint32_t) now_us);
            can_bus_port_clear();
            app_restart_learning(now_us);
            (void) console_printf("SELF_TEST,%s\r\n", enable ? "ON" : "OFF");
            break;
        }

        case (uint8_t) 'o':
        {
            fsp_err_t const err = can_bus_port_set_internal_loopback(!g_internal_loopback);
            if (FSP_SUCCESS == err)
            {
                g_internal_loopback = !g_internal_loopback;
            }
            (void) console_printf("LOOPBACK,%s,ERR,%lu\r\n",
                                  g_internal_loopback ? "ON" : "OFF",
                                  (unsigned long) err);
            break;
        }

        case (uint8_t) 'c':
        {
            fsp_err_t const err = can_bus_port_send_classic_test(g_tx_sequence++);
            (void) console_printf("TX,CLASSIC,ERR,%lu\r\n", (unsigned long) err);
            break;
        }

        case (uint8_t) 'f':
        {
            fsp_err_t const err = can_bus_port_send_fd_test(g_tx_sequence++);
            (void) console_printf("TX,CANFD_BRS,ERR,%lu\r\n", (unsigned long) err);
            break;
        }

        case (uint8_t) 'd':
        {
            uint8_t const request[8] = {0x02U, 0x10U, 0x03U, 0U, 0U, 0U, 0U, 0U};
            fsp_err_t const err = can_bus_port_send(0x7E0U,
                                                    CAN_ID_MODE_STANDARD,
                                                    false,
                                                    false,
                                                    request,
                                                    (uint8_t) sizeof(request));
            (void) console_printf("UDS_TEST,REQ,0x7E0,SERVICE,0x10,ERR,%lu\r\n",
                                  (unsigned long) err);
            break;
        }

        case (uint8_t) 'u':
            can_ui_next_page();
            (void) can_ui_render_if_dirty();
            break;

        case (uint8_t) 'i':
            app_print_diagnostics();
            break;

        case (uint8_t) 'e':
            app_dump_model_and_events();
            break;

        case (uint8_t) 'q':
            app_print_host_snapshot(now_us);
            break;

        case (uint8_t) 'h':
            app_print_help();
            break;

        case (uint8_t) '\r':
        case (uint8_t) '\n':
            break;

        default:
            (void) console_printf("COMMAND,UNKNOWN,0x%02X\r\n", (unsigned int) command);
            break;
    }
}

static void app_handle_key(uint64_t now_us)
{
    static bsp_io_level_t previous = BSP_IO_LEVEL_HIGH;
    static uint64_t pressed_us;
    static uint64_t last_transition_us;
    bsp_io_level_t level;

    if (FSP_SUCCESS != R_IOPORT_PinRead(&g_ioport_ctrl, APP_KEY_PIN, &level))
    {
        return;
    }
    if ((level == previous) || ((now_us - last_transition_us) < APP_KEY_DEBOUNCE_US))
    {
        return;
    }

    last_transition_us = now_us;
    previous = level;
    if (BSP_IO_LEVEL_LOW == level)
    {
        pressed_us = now_us;
    }
    else if ((now_us - pressed_us) >= APP_KEY_LONG_PRESS_US)
    {
        app_restart_learning(now_us);
    }
    else
    {
        can_ui_next_page();
        (void) can_ui_render_if_dirty();
    }
}

static void app_process_real_bus(uint64_t now_us)
{
    bus_port_frame_t received;
    uint32_t processed = 0U;

    while ((processed < APP_MAX_RX_PER_LOOP) && can_bus_port_pop(&received))
    {
        processed++;
        if (can_self_test_enabled(&g_self_test))
        {
            continue;
        }

        bool const extended = (CAN_ID_MODE_EXTENDED == received.frame.id_mode);
        bool const fd_frame = (0U != (received.frame.options & CANFD_FRAME_OPTION_FD));
        bool const brs = (0U != (received.frame.options & CANFD_FRAME_OPTION_BRS));
        bool const remote = (CAN_FRAME_TYPE_REMOTE == received.frame.type);
        uint8_t const flags = app_frame_flags(extended, fd_frame, brs, remote);
        uint64_t const timestamp_us = app_extend_timestamp(received.timestamp_us, now_us);

        /* AUTOSAR-style CanIf -> CanTp -> Dcm path.  It only consumes the
         * configured UDS request ID; normal IDS processing remains unchanged. */
        (void) autosar_stack_on_can_frame(received.frame.id,
                                          received.frame.data,
                                          received.data_length,
                                          app_u64_to_u32(timestamp_us / 1000ULL));

        app_process_frame(received.frame.id,
                          extended ? CAN_AI_ID_EXTENDED : CAN_AI_ID_STANDARD,
                          received.data_length,
                          flags,
                          received.frame.data,
                          timestamp_us);
    }
}

static void app_process_synthetic(uint64_t now_us)
{
    if (!can_self_test_enabled(&g_self_test))
    {
        return;
    }

    for (uint32_t count = 0U; count < APP_MAX_SYNTHETIC_PER_LOOP; count++)
    {
        can_self_test_frame_t frame;
        if (!can_self_test_next(&g_self_test, (uint32_t) now_us, &frame))
        {
            break;
        }

        if ((CAN_AI_MODE_LEARNING == g_detector.mode) &&
            (CAN_SELF_TEST_NORMAL != can_self_test_phase(&g_self_test)))
        {
            (void) app_finish_learning();
        }

        uint8_t const flags = app_frame_flags(frame.extended_id,
                                              frame.fd_frame,
                                              frame.bitrate_switch,
                                              false);
        app_process_frame(frame.id,
                          frame.extended_id ? CAN_AI_ID_EXTENDED : CAN_AI_ID_STANDARD,
                          frame.data_length_code,
                          flags,
                          frame.data,
                          app_extend_timestamp(frame.timestamp_us, now_us));
    }
}

static void app_poll_health(uint64_t now_us)
{
    if ((!g_bus_ready) || ((now_us - g_last_health_us) < APP_HEALTH_PERIOD_US))
    {
        return;
    }

    g_last_health_us = now_us;
    g_health_valid = (FSP_SUCCESS == can_bus_port_get_health(&g_bus_health));
}

static void app_auto_finish_learning(void)
{
    if (can_self_test_enabled(&g_self_test) || (CAN_AI_MODE_LEARNING != g_detector.mode))
    {
        return;
    }

    /* Count actual traffic coverage, not idle time before the first frame. */
    if ((g_detector.learning_frames >= APP_REAL_LEARNING_MIN_FRAMES) &&
        (g_detector.learning_last_us > g_detector.learning_first_us) &&
        ((g_detector.learning_last_us - g_detector.learning_first_us) >= APP_REAL_LEARNING_US))
    {
        (void) app_finish_learning();
    }
}

static void app_update_alarm(uint64_t now_us)
{
    if ((UI_ANOMALY_NONE != g_current_reason_mask) &&
        ((now_us - g_last_anomaly_us) >= APP_ALARM_HOLD_US))
    {
        g_current_reason_mask = UI_ANOMALY_NONE;
        g_current_score = 0U;
    }

    bool const alarm = app_bus_problem() || (UI_ANOMALY_NONE != g_current_reason_mask);
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl,
                             APP_LED_PIN,
                             alarm ? APP_LED_ALARM_LEVEL : APP_LED_IDLE_LEVEL);
}

void hal_entry(void)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl, APP_LED_PIN, APP_LED_IDLE_LEVEL);
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl, APP_CAN_STANDBY_PIN, BSP_IO_LEVEL_LOW);

    bool const console_ready = console_init();
    g_display_ready = can_ui_init();
    fsp_err_t const bus_open_error = can_bus_port_init();
    g_bus_ready = (FSP_SUCCESS == bus_open_error);

    bool const timebase_ready = timebase_init();
    can_bus_port_set_timestamp_provider(timebase_now_us32);
    can_bus_port_clear();

    can_ai_init(&g_detector, NULL);
    autosar_stack_init();
    can_event_log_init(&g_event_log);
    uint64_t const start_us = timebase_now_us64();
    can_self_test_init(&g_self_test, (uint32_t) start_us);
    g_last_ui_us = start_us - APP_UI_PERIOD_US;
    g_last_health_us = start_us - APP_HEALTH_PERIOD_US;
    g_metrics_started_us = start_us;

    if (console_ready)
    {
        (void) console_printf("BOOT,RA8D1_CANFD_EDGE_IDS,DISPLAY,%s,CAN,%s,ERR,%lu,TIMEBASE,%s\r\n",
                              g_display_ready ? "READY" : "ERROR",
                              g_bus_ready ? "READY" : "ERROR",
                              (unsigned long) bus_open_error,
                              timebase_ready ? "READY" : "ERROR");
        (void) console_write("PROTO,NAME,RA8D1_CAN_IDS,VERSION,1,TRANSPORT,UART,BAUD,115200\r\n");
        (void) console_write("AUTOSAR,ARCH,APP-RTE-DCM-CANTP-CANIF-MCAL,UDS_RX,0x7E0,UDS_TX,0x7E8,BOOT_SLOT,0x02010000\r\n");
        app_print_help();
    }

    if (!timebase_ready)
    {
        (void) R_IOPORT_PinWrite(&g_ioport_ctrl, APP_LED_PIN, APP_LED_ALARM_LEVEL);
        while (1)
        {
            __WFI();
        }
    }

    while (1)
    {
        timebase_poll();
        uint64_t const now_us = timebase_now_us64();

        app_handle_key(now_us);
        uint8_t command;
        while (console_read_char(&command))
        {
            app_handle_command(command, now_us);
        }

        app_process_real_bus(now_us);
        app_process_synthetic(now_us);
        autosar_stack_main_function(app_u64_to_u32(now_us / 1000ULL));
        app_auto_finish_learning();
        app_update_metrics(now_us);
        app_poll_health(now_us);
        app_update_alarm(now_us);

        if ((now_us - g_last_ui_us) >= APP_UI_PERIOD_US)
        {
            g_last_ui_us = now_us;
            app_refresh_ui(now_us);
        }

        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    }
}

#if BSP_TZ_SECURE_BUILD
FSP_CPP_HEADER
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable(void);
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable(void)
{
}
FSP_CPP_FOOTER
#endif
