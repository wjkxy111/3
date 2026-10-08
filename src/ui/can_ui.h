#ifndef CAN_UI_H
#define CAN_UI_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_UI_MAX_PROFILES     (8U)
#define CAN_UI_MAX_LIVE_FRAMES  (8U)
#define CAN_UI_MAX_EVENTS       (8U)
#define CAN_UI_DATA_PREVIEW     (8U)
#define CAN_UI_HISTORY_SAMPLES  (48U)

typedef enum
{
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_ID_PROFILES,
    UI_PAGE_LIVE_FRAMES,
    UI_PAGE_EVENTS,
    UI_PAGE_SYSTEM_HEALTH,
    UI_PAGE_COUNT
} ui_page_t;

typedef enum
{
    UI_SYSTEM_STATE_BOOT = 0,
    UI_SYSTEM_STATE_LEARNING,
    UI_SYSTEM_STATE_MONITORING,
    UI_SYSTEM_STATE_BUS_ERROR,
    UI_SYSTEM_STATE_DISPLAY_ERROR
} ui_system_state_t;

typedef enum
{
    UI_ANOMALY_NONE       = 0U,
    UI_ANOMALY_UNKNOWN_ID = (1U << 0),
    UI_ANOMALY_TIMING     = (1U << 1),
    UI_ANOMALY_FLOOD      = (1U << 2),
    UI_ANOMALY_DLC        = (1U << 3),
    UI_ANOMALY_PAYLOAD    = (1U << 4),
    UI_ANOMALY_BUS        = (1U << 5),
    UI_ANOMALY_MODEL_FULL = (1U << 6)
} ui_anomaly_reason_t;

typedef enum
{
    UI_FRAME_FLAG_NONE     = 0U,
    UI_FRAME_FLAG_EXTENDED = (1U << 0),
    UI_FRAME_FLAG_FD       = (1U << 1),
    UI_FRAME_FLAG_BRS      = (1U << 2),
    UI_FRAME_FLAG_RTR      = (1U << 3)
} ui_frame_flag_t;

typedef struct
{
    uint32_t id;
    uint32_t observed_frames;
    uint32_t period_us;
    uint16_t rate_hz_x10;
    uint16_t anomaly_score;
    uint8_t  dlc;
    uint8_t  flags;
} ui_id_profile_t;

typedef struct
{
    uint32_t id;
    uint32_t age_ms;
    uint32_t reason_mask;
    uint16_t anomaly_score;
    uint8_t  dlc;
    uint8_t  flags;
    uint8_t  data[CAN_UI_DATA_PREVIEW];
} ui_live_frame_t;

typedef struct
{
    uint32_t timestamp_ms;
    uint32_t id;
    uint32_t reason_mask;
    uint16_t anomaly_score;
    uint8_t  dlc;
    uint8_t  flags;
} ui_event_t;

/*
 * Application-owned snapshot.  The UI copies this structure in set_model(),
 * so the caller may reuse or modify its instance immediately afterwards.
 * Counts above the fixed capacities are safely clipped by the renderer.
 */
typedef struct
{
    ui_system_state_t state;
    uint32_t uptime_ms;
    uint32_t total_frames;
    uint32_t anomaly_frames;
    uint32_t unknown_frames;
    uint32_t dropped_frames;
    uint32_t bus_off_count;
    uint32_t current_reason_mask;
    uint16_t nominal_bitrate_kbps;
    uint16_t data_bitrate_kbps;
    uint16_t frames_per_second;
    uint16_t bus_load_permille;
    uint16_t anomaly_score;
    uint16_t bus_error_count;
    uint16_t display_error_code;
    uint8_t  tec;
    uint8_t  rec;
    uint8_t  learned_id_count;
    uint8_t  profile_count;
    uint8_t  live_frame_count;
    uint8_t  event_count;
    bool     demo_enabled;
    bool     loopback_enabled;
    bool     health_valid;
    bool     can_fd_enabled;
    ui_id_profile_t profiles[CAN_UI_MAX_PROFILES];
    ui_live_frame_t live_frames[CAN_UI_MAX_LIVE_FRAMES];
    ui_event_t      events[CAN_UI_MAX_EVENTS];
} ui_view_model_t;

/* Initializes the display port if necessary and schedules a full redraw. */
bool can_ui_init(void);

/* Replaces the UI snapshot and schedules a redraw. */
void can_ui_set_model(ui_view_model_t const * p_model);

/* Short-press action: Dashboard -> Profiles -> Live -> Events -> Dashboard. */
void can_ui_next_page(void);

/* Forces the current page to be redrawn on the next render call. */
void can_ui_mark_dirty(void);

/* Returns false only when the display is unavailable; a clean frame is success. */
bool can_ui_render_if_dirty(void);

#endif /* CAN_UI_H */
