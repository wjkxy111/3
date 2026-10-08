#include "can_ui.h"

#include <stddef.h>
#include <string.h>

#include "../display/display_port.h"

#define UI_WIDTH   ((int16_t) DISPLAY_PORT_WIDTH)
#define UI_HEIGHT  ((int16_t) DISPLAY_PORT_HEIGHT)

#define RGB565_BLACK       (0x0000U)
#define RGB565_BACKGROUND  (0x0841U)
#define RGB565_PANEL       (0x10C3U)
#define RGB565_PANEL_ALT   (0x1925U)
#define RGB565_BORDER      (0x29A7U)
#define RGB565_TEXT        (0xE73CU)
#define RGB565_TEXT_DIM    (0x8410U)
#define RGB565_CYAN        (0x2E9FU)
#define RGB565_BLUE        (0x54BFU)
#define RGB565_GREEN       (0x4E69U)
#define RGB565_AMBER       (0xFCE0U)
#define RGB565_RED         (0xF986U)
#define RGB565_MAGENTA     (0xD8FFU)

#define UI_SCORE_MAX       (1000U)
#define UI_LOAD_MAX        (1000U)

typedef struct
{
    char character;
    uint8_t columns[5];
} ui_glyph_t;

typedef enum
{
    UI_CJK_EDGE_AI = 0,
    UI_CJK_MONITOR,
    UI_CJK_LEARNING,
    UI_CJK_ALARM,
    UI_CJK_PROFILE,
    UI_CJK_REALTIME,
    UI_CJK_EVENT,
    UI_CJK_HEALTH
} ui_cjk_label_t;

typedef struct
{
    uint16_t rows[16];
} ui_cjk_glyph_t;

/* 16x16 monochrome subset generated from SimHei; only labels used by this UI are stored. */
static ui_cjk_glyph_t const g_cjk16[] =
{
    {{0x6490U,0x2490U,0x0490U,0xF360U,0x97F0U,0x9080U,0x9100U,0x67F0U,0x2550U,0x7550U,0x8550U,0x0430U,0,0,0,0}}, /* 端 */
    {{0x2010U,0x5F10U,0x5150U,0xD550U,0xD550U,0x5550U,0x5550U,0x5550U,0x4450U,0x4A10U,0x4910U,0x7130U,0,0,0,0}}, /* 侧 */
    {{0x4980U,0x4900U,0x49F0U,0x4B80U,0x4A40U,0x4E20U,0,0x7FE0U,0x4920U,0x4920U,0x4920U,0xFFF8U,0,0,0,0}}, /* 监 */
    {{0x8010U,0x7F10U,0x1150U,0x9550U,0xD550U,0x5550U,0x1550U,0x7550U,0x4450U,0xCA10U,0x8910U,0x9130U,0,0,0,0}}, /* 测 */
    {{0x4440U,0x26C0U,0x0080U,0xFFF0U,0x8010U,0x3FC0U,0x0180U,0x0200U,0xFFF0U,0x0200U,0x0200U,0x0E00U,0x0800U,0,0,0}}, /* 学 */
    {{0,0x7FE0U,0x0020U,0x3020U,0x0C20U,0x0420U,0x0020U,0x0320U,0x1C20U,0x7020U,0x0060U,0x01C0U,0,0,0,0}}, /* 习 */
    {{0x2200U,0x2200U,0x7FE0U,0x4200U,0x0200U,0xFFF0U,0,0x3FC0U,0x2040U,0x2040U,0x3FC0U,0x2040U,0,0,0,0}}, /* 告 */
    {{0xFE80U,0x29F0U,0x7F20U,0xF4C0U,0x54E0U,0x2510U,0xFFF0U,0x3FC0U,0,0x1F80U,0x3FC0U,0x3FC0U,0,0,0,0}}, /* 警 */
    {{0xFFF0U,0,0,0x1F80U,0x9290U,0x9F90U,0x9290U,0x9290U,0x9F90U,0x8010U,0x8010U,0x7FF0U,0,0,0,0}}, /* 画 */
    {{0x2600U,0x27C0U,0x4880U,0x5F60U,0xC920U,0xC7C0U,0x4F60U,0x4BC0U,0x4DC0U,0x52A0U,0x4C90U,0x4380U,0,0,0,0}}, /* 像 */
    {{0x0600U,0xFFF0U,0x4030U,0x5130U,0x1900U,0x6100U,0x3100U,0x0200U,0xFFF0U,0x0700U,0x0CC0U,0xF030U,0,0,0,0}}, /* 实 */
    {{0x0020U,0xF820U,0x9020U,0x97F8U,0xF020U,0x9220U,0x9320U,0x9120U,0xF020U,0x9820U,0x8020U,0x00E0U,0,0,0,0}}, /* 时 */
    {{0x0400U,0xFFF0U,0x3FC0U,0x2440U,0x3FC0U,0x0400U,0x7FC0U,0xFFF0U,0x0440U,0x7FC0U,0x0440U,0x0C00U,0,0,0,0}}, /* 事 */
    {{0x2080U,0x2480U,0x6880U,0x6FF0U,0xA880U,0x3080U,0x2080U,0x3FF0U,0x2080U,0x2080U,0x2080U,0x6080U,0,0,0,0}}, /* 件 */
    {{0x4080U,0x5FE0U,0x48A0U,0xCBF0U,0xD0A0U,0x5FE0U,0x4880U,0x5BF0U,0x5880U,0x59E0U,0x5C80U,0x73F0U,0,0,0,0}}, /* 健 */
    {{0x0200U,0x7FF0U,0x4200U,0x5FE0U,0x6220U,0x5FF0U,0x5FE0U,0x6340U,0x5BE0U,0x4EC0U,0x9260U,0x8610U,0,0,0,0}}  /* 康 */
};

static uint8_t const g_cjk_labels[][2] =
{
    {0U, 1U}, {2U, 3U}, {4U, 5U}, {6U, 7U},
    {8U, 9U}, {10U, 11U}, {12U, 13U}, {14U, 15U}
};

/* Five columns, least-significant bit at the top.  Only UI glyphs are kept. */
static ui_glyph_t const g_font5x7[] =
{
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'#', {0x14, 0x7F, 0x14, 0x7F, 0x14}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'(', {0x00, 0x1C, 0x22, 0x41, 0x00}},
    {')', {0x00, 0x41, 0x22, 0x1C, 0x00}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {',', {0x00, 0x50, 0x30, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'<', {0x08, 0x14, 0x22, 0x41, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'[', {0x00, 0x7F, 0x41, 0x41, 0x00}},
    {']', {0x00, 0x41, 0x41, 0x7F, 0x00}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'|', {0x00, 0x00, 0x7F, 0x00, 0x00}}
};

static ui_view_model_t g_model;
static ui_page_t g_page;
static bool g_dirty;
static bool g_initialized;
static uint16_t g_load_history[CAN_UI_HISTORY_SAMPLES];
static uint16_t g_score_history[CAN_UI_HISTORY_SAMPLES];
static uint8_t g_history_count;
static uint8_t g_history_head;
static uint32_t g_last_history_ms;

static uint8_t const * glyph_columns(char character)
{
    for (size_t i = 0U; i < (sizeof(g_font5x7) / sizeof(g_font5x7[0])); i++)
    {
        if (g_font5x7[i].character == character)
        {
            return g_font5x7[i].columns;
        }
    }

    for (size_t i = 0U; i < (sizeof(g_font5x7) / sizeof(g_font5x7[0])); i++)
    {
        if ('?' == g_font5x7[i].character)
        {
            return g_font5x7[i].columns;
        }
    }

    return g_font5x7[0].columns;
}

static void fill_rect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color)
{
    uint16_t * const framebuffer = display_port_framebuffer();
    if ((NULL == framebuffer) || (width <= 0) || (height <= 0))
    {
        return;
    }

    int16_t x0 = (x < 0) ? 0 : x;
    int16_t y0 = (y < 0) ? 0 : y;
    int16_t x1 = (int16_t) (x + width);
    int16_t y1 = (int16_t) (y + height);
    x1 = (x1 > UI_WIDTH) ? UI_WIDTH : x1;
    y1 = (y1 > UI_HEIGHT) ? UI_HEIGHT : y1;

    for (int16_t row = y0; row < y1; row++)
    {
        uint16_t * pixel = &framebuffer[((uint32_t) row * DISPLAY_PORT_WIDTH) + (uint16_t) x0];
        for (int16_t column = x0; column < x1; column++)
        {
            *pixel++ = color;
        }
    }
}

static void draw_rect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color)
{
    fill_rect(x, y, width, 1, color);
    fill_rect(x, (int16_t) (y + height - 1), width, 1, color);
    fill_rect(x, y, 1, height, color);
    fill_rect((int16_t) (x + width - 1), y, 1, height, color);
}

static void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 >= x0) ? (int16_t) (x1 - x0) : (int16_t) (x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy_abs = (y1 >= y0) ? (int16_t) (y1 - y0) : (int16_t) (y0 - y1);
    int16_t dy = (int16_t) -dy_abs;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t error = (int16_t) (dx + dy);

    while (true)
    {
        fill_rect(x0, y0, 1, 1, color);
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        int16_t const twice_error = (int16_t) (2 * error);
        if (twice_error >= dy)
        {
            error = (int16_t) (error + dy);
            x0 = (int16_t) (x0 + sx);
        }
        if (twice_error <= dx)
        {
            error = (int16_t) (error + dx);
            y0 = (int16_t) (y0 + sy);
        }
    }
}

static void draw_circle(int16_t center_x, int16_t center_y, int16_t radius, uint16_t color)
{
    int16_t x = radius;
    int16_t y = 0;
    int16_t error = 0;

    while (x >= y)
    {
        fill_rect((int16_t) (center_x + x), (int16_t) (center_y + y), 1, 1, color);
        fill_rect((int16_t) (center_x + y), (int16_t) (center_y + x), 1, 1, color);
        fill_rect((int16_t) (center_x - y), (int16_t) (center_y + x), 1, 1, color);
        fill_rect((int16_t) (center_x - x), (int16_t) (center_y + y), 1, 1, color);
        fill_rect((int16_t) (center_x - x), (int16_t) (center_y - y), 1, 1, color);
        fill_rect((int16_t) (center_x - y), (int16_t) (center_y - x), 1, 1, color);
        fill_rect((int16_t) (center_x + y), (int16_t) (center_y - x), 1, 1, color);
        fill_rect((int16_t) (center_x + x), (int16_t) (center_y - y), 1, 1, color);
        y++;
        if (error <= 0)
        {
            error = (int16_t) (error + (2 * y) + 1);
        }
        if (error > 0)
        {
            x--;
            error = (int16_t) (error - (2 * x) - 1);
        }
    }
}

static void draw_character(int16_t x, int16_t y, char character, uint8_t scale, uint16_t color)
{
    uint8_t const * const columns = glyph_columns(character);
    for (uint8_t column = 0U; column < 5U; column++)
    {
        uint8_t bits = columns[column];
        for (uint8_t row = 0U; row < 7U; row++)
        {
            if (0U != (bits & (uint8_t) (1U << row)))
            {
                fill_rect((int16_t) (x + ((int16_t) column * scale)),
                          (int16_t) (y + ((int16_t) row * scale)), scale, scale, color);
            }
        }
    }
}

static void draw_cjk_label(int16_t x, int16_t y, ui_cjk_label_t label, uint16_t color)
{
    if ((uint32_t) label >= (sizeof(g_cjk_labels) / sizeof(g_cjk_labels[0])))
    {
        return;
    }
    for (uint8_t glyph = 0U; glyph < 2U; glyph++)
    {
        ui_cjk_glyph_t const * const source = &g_cjk16[g_cjk_labels[label][glyph]];
        for (uint8_t row = 0U; row < 16U; row++)
        {
            uint16_t const bits = source->rows[row];
            for (uint8_t column = 0U; column < 16U; column++)
            {
                if (0U != (bits & (uint16_t) (0x8000U >> column)))
                {
                    fill_rect((int16_t) (x + column), (int16_t) (y + row), 1, 1, color);
                }
            }
        }
        x = (int16_t) (x + 17);
    }
}

static int16_t draw_text(int16_t x, int16_t y, char const * text, uint8_t scale, uint16_t color)
{
    if (NULL == text)
    {
        return x;
    }

    int16_t cursor = x;
    while ('\0' != *text)
    {
        draw_character(cursor, y, *text, scale, color);
        cursor = (int16_t) (cursor + ((int16_t) 6 * scale));
        text++;
    }
    return cursor;
}

static int16_t draw_u32(int16_t x, int16_t y, uint32_t value, uint8_t minimum_digits,
                        uint8_t scale, uint16_t color)
{
    char digits[10];
    uint8_t length = 0U;
    do
    {
        digits[length++] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while ((value > 0U) && (length < sizeof(digits)));

    while ((length < minimum_digits) && (length < sizeof(digits)))
    {
        digits[length++] = '0';
    }

    while (length > 0U)
    {
        length--;
        draw_character(x, y, digits[length], scale, color);
        x = (int16_t) (x + ((int16_t) 6 * scale));
    }
    return x;
}

static int16_t draw_hex(int16_t x, int16_t y, uint32_t value, uint8_t digits,
                        uint8_t scale, uint16_t color)
{
    for (uint8_t position = digits; position > 0U; position--)
    {
        uint8_t const shift = (uint8_t) ((position - 1U) * 4U);
        uint8_t const nibble = (uint8_t) ((value >> shift) & 0x0FU);
        char const character = (nibble < 10U) ? (char) ('0' + nibble) : (char) ('A' + nibble - 10U);
        draw_character(x, y, character, scale, color);
        x = (int16_t) (x + ((int16_t) 6 * scale));
    }
    return x;
}

static int16_t draw_fixed_one(int16_t x, int16_t y, uint32_t value_x10,
                              uint8_t scale, uint16_t color)
{
    x = draw_u32(x, y, value_x10 / 10U, 1U, scale, color);
    x = draw_text(x, y, ".", scale, color);
    return draw_u32(x, y, value_x10 % 10U, 1U, scale, color);
}

static uint16_t state_color(ui_system_state_t state)
{
    switch (state)
    {
        case UI_SYSTEM_STATE_LEARNING:
            return RGB565_BLUE;
        case UI_SYSTEM_STATE_MONITORING:
            return RGB565_GREEN;
        case UI_SYSTEM_STATE_BUS_ERROR:
            return RGB565_RED;
        case UI_SYSTEM_STATE_DISPLAY_ERROR:
            return RGB565_MAGENTA;
        case UI_SYSTEM_STATE_BOOT:
        default:
            return RGB565_AMBER;
    }
}

static char const * state_label(ui_system_state_t state)
{
    switch (state)
    {
        case UI_SYSTEM_STATE_LEARNING:
            return "LEARN";
        case UI_SYSTEM_STATE_MONITORING:
            return "MONITOR";
        case UI_SYSTEM_STATE_BUS_ERROR:
            return "BUS ERR";
        case UI_SYSTEM_STATE_DISPLAY_ERROR:
            return "LCD ERR";
        case UI_SYSTEM_STATE_BOOT:
        default:
            return "BOOT";
    }
}

static uint16_t score_color(uint16_t score)
{
    if (score >= 700U)
    {
        return RGB565_RED;
    }
    if (score >= 400U)
    {
        return RGB565_AMBER;
    }
    return RGB565_GREEN;
}

static void history_push(ui_view_model_t const * model)
{
    if ((0U != g_history_count) && ((model->uptime_ms - g_last_history_ms) < 1000U))
    {
        return;
    }

    g_load_history[g_history_head] = (model->bus_load_permille > UI_LOAD_MAX) ?
                                     UI_LOAD_MAX : model->bus_load_permille;
    g_score_history[g_history_head] = (model->anomaly_score > UI_SCORE_MAX) ?
                                      UI_SCORE_MAX : model->anomaly_score;
    g_history_head = (uint8_t) ((g_history_head + 1U) % CAN_UI_HISTORY_SAMPLES);
    if (g_history_count < CAN_UI_HISTORY_SAMPLES)
    {
        g_history_count++;
    }
    g_last_history_ms = model->uptime_ms;
}

static uint16_t history_value(uint16_t const * history, uint8_t logical_index)
{
    uint8_t const oldest = (g_history_count < CAN_UI_HISTORY_SAMPLES) ?
                           0U : g_history_head;
    uint8_t const index = (uint8_t) ((oldest + logical_index) % CAN_UI_HISTORY_SAMPLES);
    return history[index];
}

static void draw_trend_chart(int16_t x, int16_t y, int16_t width, int16_t height)
{
    fill_rect(x, y, width, height, RGB565_BLACK);
    draw_rect(x, y, width, height, RGB565_BORDER);
    for (uint8_t grid = 1U; grid < 4U; grid++)
    {
        int16_t const grid_y = (int16_t) (y + ((height * grid) / 4));
        for (int16_t grid_x = (int16_t) (x + 2); grid_x < (int16_t) (x + width - 2); grid_x += 6)
        {
            fill_rect(grid_x, grid_y, 2, 1, RGB565_PANEL_ALT);
        }
    }

    if (g_history_count < 2U)
    {
        (void) draw_text((int16_t) (x + 49), (int16_t) (y + 24), "COLLECTING TREND", 1U, RGB565_TEXT_DIM);
        return;
    }

    uint32_t const plot_width = (uint32_t) (width - 5);
    uint32_t const plot_height = (uint32_t) (height - 5);
    int32_t const plot_bottom = (int32_t) y + height - 3;
    int16_t previous_x = (int16_t) (x + 2);
    uint16_t previous_load = history_value(g_load_history, 0U);
    uint16_t previous_score = history_value(g_score_history, 0U);
    int16_t previous_load_y = (int16_t) (plot_bottom -
                              (int32_t) (plot_height * previous_load / UI_LOAD_MAX));
    int16_t previous_score_y = (int16_t) (plot_bottom -
                               (int32_t) (plot_height * previous_score / UI_SCORE_MAX));

    for (uint8_t index = 1U; index < g_history_count; index++)
    {
        int16_t const point_x = (int16_t) ((int32_t) x + 2 +
                                (int32_t) ((plot_width * index) / (g_history_count - 1U)));
        uint16_t const load = history_value(g_load_history, index);
        uint16_t const score = history_value(g_score_history, index);
        int16_t const load_y = (int16_t) (plot_bottom -
                               (int32_t) (plot_height * load / UI_LOAD_MAX));
        int16_t const score_y = (int16_t) (plot_bottom -
                                (int32_t) (plot_height * score / UI_SCORE_MAX));
        draw_line(previous_x, previous_load_y, point_x, load_y, RGB565_CYAN);
        draw_line(previous_x, previous_score_y, point_x, score_y, score_color(score));
        previous_x = point_x;
        previous_load_y = load_y;
        previous_score_y = score_y;
    }
}

static void draw_health_badge(int16_t x, int16_t y, char const * label, bool good)
{
    uint16_t const color = good ? RGB565_GREEN : RGB565_RED;
    draw_circle((int16_t) (x + 8), (int16_t) (y + 8), 7, color);
    fill_rect((int16_t) (x + 6), (int16_t) (y + 6), 5, 5, color);
    (void) draw_text((int16_t) (x + 21), (int16_t) (y + 4), label, 1U, good ? RGB565_TEXT : color);
}

static void draw_bar(int16_t x, int16_t y, int16_t width, uint32_t value,
                     uint32_t maximum, uint16_t color)
{
    fill_rect(x, y, width, 7, RGB565_BLACK);
    draw_rect(x, y, width, 7, RGB565_BORDER);
    if ((maximum > 0U) && (value > 0U) && (width > 4))
    {
        value = (value > maximum) ? maximum : value;
        int16_t const filled = (int16_t) (((uint32_t) (width - 4) * value) / maximum);
        fill_rect((int16_t) (x + 2), (int16_t) (y + 2), filled, 3, color);
    }
}

static void draw_panel(int16_t x, int16_t y, int16_t width, int16_t height)
{
    fill_rect(x, y, width, height, RGB565_PANEL);
    draw_rect(x, y, width, height, RGB565_BORDER);
}

static void draw_frame_flags(int16_t x, int16_t y, uint8_t flags, uint16_t color)
{
    if (0U != (flags & UI_FRAME_FLAG_EXTENDED))
    {
        x = draw_text(x, y, "X", 1U, color);
    }
    if (0U != (flags & UI_FRAME_FLAG_FD))
    {
        x = draw_text(x, y, "F", 1U, color);
    }
    if (0U != (flags & UI_FRAME_FLAG_BRS))
    {
        x = draw_text(x, y, "B", 1U, color);
    }
    if (0U != (flags & UI_FRAME_FLAG_RTR))
    {
        (void) draw_text(x, y, "R", 1U, color);
    }
}

static char const * reason_token(uint32_t bit)
{
    switch (bit)
    {
        case UI_ANOMALY_UNKNOWN_ID:
            return "NEW ID";
        case UI_ANOMALY_TIMING:
            return "TIMING";
        case UI_ANOMALY_FLOOD:
            return "FLOOD";
        case UI_ANOMALY_DLC:
            return "DLC";
        case UI_ANOMALY_PAYLOAD:
            return "PAYLOAD";
        case UI_ANOMALY_BUS:
            return "BUS";
        case UI_ANOMALY_MODEL_FULL:
            return "MODEL FULL";
        default:
            return "NORMAL";
    }
}

static void draw_reason_tokens(int16_t x, int16_t y, uint32_t mask, int16_t right_edge,
                               uint16_t color)
{
    static uint32_t const reason_bits[] =
    {
        UI_ANOMALY_UNKNOWN_ID,
        UI_ANOMALY_TIMING,
        UI_ANOMALY_FLOOD,
        UI_ANOMALY_DLC,
        UI_ANOMALY_PAYLOAD,
        UI_ANOMALY_BUS,
        UI_ANOMALY_MODEL_FULL
    };

    if (UI_ANOMALY_NONE == mask)
    {
        (void) draw_text(x, y, "NORMAL", 1U, RGB565_GREEN);
        return;
    }

    bool separator = false;
    for (size_t i = 0U; i < (sizeof(reason_bits) / sizeof(reason_bits[0])); i++)
    {
        if (0U != (mask & reason_bits[i]))
        {
            char const * const token = reason_token(reason_bits[i]);
            size_t const length = strlen(token);
            int16_t const required = (int16_t) ((length * 6U) + (separator ? 6U : 0U));
            if ((int16_t) (x + required) > right_edge)
            {
                if ((int16_t) (x + 12) <= right_edge)
                {
                    (void) draw_text(x, y, "+", 1U, color);
                }
                break;
            }
            if (separator)
            {
                x = draw_text(x, y, "|", 1U, RGB565_TEXT_DIM);
            }
            x = draw_text(x, y, token, 1U, color);
            separator = true;
        }
    }
}

static void draw_header(void)
{
    uint16_t const accent = state_color(g_model.state);
    fill_rect(0, 0, UI_WIDTH, 5, accent);
    draw_cjk_label(9, 11, UI_CJK_EDGE_AI, RGB565_TEXT);
    (void) draw_text(49, 15, "CAN-FD IDS", 1U, RGB565_CYAN);

    ui_cjk_label_t page_label = UI_CJK_MONITOR;
    switch (g_page)
    {
        case UI_PAGE_ID_PROFILES:
            page_label = UI_CJK_PROFILE;
            break;
        case UI_PAGE_LIVE_FRAMES:
            page_label = UI_CJK_REALTIME;
            break;
        case UI_PAGE_EVENTS:
            page_label = UI_CJK_EVENT;
            break;
        case UI_PAGE_SYSTEM_HEALTH:
            page_label = UI_CJK_HEALTH;
            break;
        case UI_PAGE_DASHBOARD:
        default:
            break;
    }
    draw_cjk_label(166, 11, page_label, accent);

    int16_t x = draw_u32(218, 14, (uint32_t) g_page + 1U, 1U, 1U, RGB565_TEXT_DIM);
    x = draw_text(x, 14, "/", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 14, UI_PAGE_COUNT, 1U, 1U, RGB565_TEXT_DIM);
    fill_rect(8, 35, 240, 1, RGB565_BORDER);

    draw_panel(8, 43, 240, 42);
    (void) draw_text(15, 51, "CANFD1", 1U, RGB565_CYAN);

    uint16_t const nominal = (0U == g_model.nominal_bitrate_kbps) ? 500U : g_model.nominal_bitrate_kbps;
    uint16_t const data = (0U == g_model.data_bitrate_kbps) ? 2000U : g_model.data_bitrate_kbps;
    x = draw_u32(59, 51, nominal, 1U, 1U, RGB565_TEXT);
    x = draw_text(x, 51, "K/", 1U, RGB565_TEXT_DIM);
    if ((data >= 1000U) && (0U == (data % 1000U)))
    {
        x = draw_u32(x, 51, data / 1000U, 1U, 1U, RGB565_TEXT);
        (void) draw_text(x, 51, "M", 1U, RGB565_TEXT_DIM);
    }
    else
    {
        x = draw_u32(x, 51, data, 1U, 1U, RGB565_TEXT);
        (void) draw_text(x, 51, "K", 1U, RGB565_TEXT_DIM);
    }

    fill_rect(165, 48, 75, 17, accent);
    if (UI_SYSTEM_STATE_LEARNING == g_model.state)
    {
        draw_cjk_label(185, 49, UI_CJK_LEARNING, RGB565_BLACK);
    }
    else if (UI_SYSTEM_STATE_MONITORING == g_model.state)
    {
        draw_cjk_label(185, 49, UI_CJK_MONITOR, RGB565_BLACK);
    }
    else if ((UI_SYSTEM_STATE_BUS_ERROR == g_model.state) ||
             (UI_SYSTEM_STATE_DISPLAY_ERROR == g_model.state))
    {
        draw_cjk_label(185, 49, UI_CJK_ALARM, RGB565_BLACK);
    }
    else
    {
        (void) draw_text(190, 53, state_label(g_model.state), 1U, RGB565_BLACK);
    }

    x = draw_text(15, 70, "UP ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 70, g_model.uptime_ms / 1000U, 1U, 1U, RGB565_TEXT);
    x = draw_text(x, 70, "S  RX ", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 70, g_model.total_frames, 1U, 1U, RGB565_TEXT);

    if (UI_SYSTEM_STATE_BUS_ERROR == g_model.state)
    {
        x = draw_text(168, 70, "E", 1U, RGB565_RED);
        (void) draw_u32(x, 70, g_model.bus_error_count, 1U, 1U, RGB565_RED);
    }
    else if (UI_SYSTEM_STATE_DISPLAY_ERROR == g_model.state)
    {
        x = draw_text(168, 70, "LCD ", 1U, RGB565_MAGENTA);
        (void) draw_u32(x, 70, g_model.display_error_code, 1U, 1U, RGB565_MAGENTA);
    }
}

static void draw_footer(void)
{
    fill_rect(0, 455, UI_WIDTH, 25, RGB565_PANEL_ALT);
    fill_rect(0, 455, UI_WIDTH, 1, RGB565_BORDER);
    (void) draw_text(9, 464, "S1 NEXT | HOLD: LEARN", 1U, RGB565_TEXT_DIM);

    int16_t x = 187;
    for (uint8_t page = 0U; page < (uint8_t) UI_PAGE_COUNT; page++)
    {
        uint16_t const color = (page == (uint8_t) g_page) ? state_color(g_model.state) : RGB565_BORDER;
        fill_rect(x, 464, 6, 7, color);
        x = (int16_t) (x + 12);
    }
}

static void draw_metric_card(int16_t x, char const * label, uint32_t value_x10,
                             uint16_t color, bool percent)
{
    draw_panel(x, 94, 116, 72);
    (void) draw_text((int16_t) (x + 8), 102, label, 1U, RGB565_TEXT_DIM);
    int16_t cursor = draw_fixed_one((int16_t) (x + 8), 119, value_x10, 2U, color);
    if (percent)
    {
        (void) draw_text(cursor, 119, "%", 2U, color);
    }
    draw_bar((int16_t) (x + 8), 151, 100, value_x10, UI_LOAD_MAX, color);
}

static void draw_dashboard(void)
{
    uint16_t const load = (g_model.bus_load_permille > UI_LOAD_MAX) ? UI_LOAD_MAX : g_model.bus_load_permille;
    uint16_t const score = (g_model.anomaly_score > UI_SCORE_MAX) ? UI_SCORE_MAX : g_model.anomaly_score;
    draw_metric_card(8, "BUS LOAD", load, (load >= 800U) ? RGB565_AMBER : RGB565_CYAN, true);
    draw_metric_card(132, "RISK", score, score_color(score), true);

    if ((g_model.anomaly_score >= 700U) ||
        (UI_SYSTEM_STATE_BUS_ERROR == g_model.state) ||
        (UI_SYSTEM_STATE_DISPLAY_ERROR == g_model.state))
    {
        uint16_t alert_color = score_color(g_model.anomaly_score);
        if (UI_SYSTEM_STATE_BUS_ERROR == g_model.state)
        {
            alert_color = RGB565_RED;
        }
        else if (UI_SYSTEM_STATE_DISPLAY_ERROR == g_model.state)
        {
            alert_color = RGB565_MAGENTA;
        }
        fill_rect(8, 168, 240, 5, alert_color);
    }

    static char const * const metric_labels[] = {"FPS", "IDS", "TEC", "REC"};
    uint32_t const metric_values[] =
    {
        g_model.frames_per_second,
        g_model.learned_id_count,
        g_model.tec,
        g_model.rec
    };
    for (uint8_t i = 0U; i < 4U; i++)
    {
        int16_t const x = (int16_t) (8 + ((int16_t) i * 61));
        draw_panel(x, 173, 55, 55);
        (void) draw_text((int16_t) (x + 6), 181, metric_labels[i], 1U, RGB565_TEXT_DIM);
        (void) draw_u32((int16_t) (x + 6), 198, metric_values[i], 1U, 2U,
                        (i >= 2U && metric_values[i] > 0U) ? RGB565_RED : RGB565_TEXT);
    }

    draw_panel(8, 236, 240, 58);
    (void) draw_text(15, 245, "ACTIVE REASON", 1U, RGB565_TEXT_DIM);
    draw_reason_tokens(15, 266, g_model.current_reason_mask, 241,
                       score_color(g_model.anomaly_score));

    draw_panel(8, 302, 240, 101);
    (void) draw_text(15, 311, "LATEST FRAME", 1U, RGB565_TEXT_DIM);
    if (g_model.live_frame_count > 0U)
    {
        ui_live_frame_t const * const frame = &g_model.live_frames[0];
        int16_t x = draw_text(15, 330, "ID ", 1U, RGB565_TEXT_DIM);
        x = draw_hex(x, 330, frame->id,
                     (0U != (frame->flags & UI_FRAME_FLAG_EXTENDED)) ? 8U : 3U,
                     1U, RGB565_CYAN);
        x = draw_text((int16_t) (x + 7), 330, "DLC ", 1U, RGB565_TEXT_DIM);
        (void) draw_u32(x, 330, frame->dlc, 1U, 1U, RGB565_TEXT);
        draw_frame_flags(202, 330, frame->flags, RGB565_AMBER);

        x = 15;
        uint8_t const bytes = (frame->dlc < CAN_UI_DATA_PREVIEW) ? frame->dlc : CAN_UI_DATA_PREVIEW;
        for (uint8_t i = 0U; i < bytes; i++)
        {
            x = draw_hex(x, 352, frame->data[i], 2U, 1U, RGB565_TEXT);
            x = (int16_t) (x + 4);
        }
        if (0U == bytes)
        {
            (void) draw_text(15, 352, "NO DATA", 1U, RGB565_TEXT_DIM);
        }
        x = draw_text(15, 377, "AGE ", 1U, RGB565_TEXT_DIM);
        x = draw_u32(x, 377, frame->age_ms, 1U, 1U, RGB565_TEXT);
        (void) draw_text(x, 377, "MS", 1U, RGB565_TEXT_DIM);
    }
    else
    {
        (void) draw_text(15, 340, "WAITING FOR CAN TRAFFIC", 1U, RGB565_TEXT_DIM);
    }

    draw_panel(8, 411, 240, 36);
    int16_t x = draw_text(15, 423, "DROP ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 423, g_model.dropped_frames, 1U, 1U,
                 (g_model.dropped_frames > 0U) ? RGB565_AMBER : RGB565_TEXT);
    x = draw_text((int16_t) (x + 12), 423, "ERR ", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 423, g_model.bus_error_count, 1U, 1U,
                    (g_model.bus_error_count > 0U) ? RGB565_RED : RGB565_TEXT);
}

static void draw_system_health(void)
{
    draw_panel(8, 94, 240, 81);
    (void) draw_text(15, 103, "48S LOAD / RISK TREND", 1U, RGB565_TEXT_DIM);
    draw_trend_chart(15, 120, 226, 48);
    fill_rect(18, 110, 8, 3, RGB565_CYAN);
    (void) draw_text(29, 107, "LOAD", 1U, RGB565_CYAN);
    fill_rect(78, 110, 8, 3, score_color(g_model.anomaly_score));
    (void) draw_text(89, 107, "RISK", 1U, score_color(g_model.anomaly_score));

    draw_panel(8, 183, 240, 89);
    (void) draw_text(15, 192, "SYSTEM HEALTH", 1U, RGB565_TEXT_DIM);
    bool const can_ok = (UI_SYSTEM_STATE_BUS_ERROR != g_model.state);
    bool const display_ok = (UI_SYSTEM_STATE_DISPLAY_ERROR != g_model.state);
    bool const queue_ok = (0U == g_model.dropped_frames);
    draw_health_badge(15, 213, "CAN BUS", can_ok);
    draw_health_badge(133, 213, "DISPLAY", display_ok);
    draw_health_badge(15, 242, "RX QUEUE", queue_ok);
    if (g_model.health_valid)
    {
        draw_health_badge(133, 242, "CTRL INFO", 0U == g_model.bus_off_count);
    }
    else
    {
        draw_circle(141, 250, 7, RGB565_AMBER);
        fill_rect(139, 248, 5, 5, RGB565_AMBER);
        (void) draw_text(154, 246, "CTRL WAIT", 1U, RGB565_AMBER);
    }

    draw_panel(8, 280, 240, 64);
    (void) draw_text(15, 289, "RUNTIME MODE", 1U, RGB565_TEXT_DIM);
    int16_t x = draw_text(15, 310, "SOURCE ", 1U, RGB565_TEXT_DIM);
    x = draw_text(x, 310, g_model.demo_enabled ? "SYNTH DEMO" : "PHYSICAL CAN", 1U,
                  g_model.demo_enabled ? RGB565_MAGENTA : RGB565_CYAN);
    x = draw_text(15, 327, "LOOPBACK ", 1U, RGB565_TEXT_DIM);
    (void) draw_text(x, 327, g_model.loopback_enabled ? "ON" : "OFF", 1U,
                     g_model.loopback_enabled ? RGB565_AMBER : RGB565_GREEN);

    draw_panel(8, 352, 240, 95);
    (void) draw_text(15, 361, "LIFETIME COUNTERS", 1U, RGB565_TEXT_DIM);
    x = draw_text(15, 382, "ANOM ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 382, g_model.anomaly_frames, 1U, 1U,
                 (g_model.anomaly_frames > 0U) ? RGB565_AMBER : RGB565_TEXT);
    x = draw_text(132, 382, "UNKNOWN ", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 382, g_model.unknown_frames, 1U, 1U,
                    (g_model.unknown_frames > 0U) ? RGB565_AMBER : RGB565_TEXT);
    x = draw_text(15, 405, "DROP ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 405, g_model.dropped_frames, 1U, 1U,
                 (g_model.dropped_frames > 0U) ? RGB565_RED : RGB565_TEXT);
    x = draw_text(132, 405, "BUSOFF ", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 405, g_model.bus_off_count, 1U, 1U,
                    (g_model.bus_off_count > 0U) ? RGB565_RED : RGB565_TEXT);
    x = draw_text(15, 428, "TEC ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 428, g_model.tec, 1U, 1U, (g_model.tec > 0U) ? RGB565_RED : RGB565_TEXT);
    x = draw_text(82, 428, "REC ", 1U, RGB565_TEXT_DIM);
    x = draw_u32(x, 428, g_model.rec, 1U, 1U, (g_model.rec > 0U) ? RGB565_RED : RGB565_TEXT);
    x = draw_text(152, 428, "IDS ", 1U, RGB565_TEXT_DIM);
    (void) draw_u32(x, 428, g_model.learned_id_count, 1U, 1U, RGB565_TEXT);
}

static void draw_profiles(void)
{
    draw_panel(8, 94, 240, 25);
    (void) draw_text(13, 103, "ID", 1U, RGB565_TEXT_DIM);
    (void) draw_text(77, 103, "DLC", 1U, RGB565_TEXT_DIM);
    (void) draw_text(108, 103, "FPS", 1U, RGB565_TEXT_DIM);
    (void) draw_text(151, 103, "PERIOD", 1U, RGB565_TEXT_DIM);
    (void) draw_text(211, 103, "RISK", 1U, RGB565_TEXT_DIM);

    uint8_t const count = (g_model.profile_count > CAN_UI_MAX_PROFILES) ?
                          CAN_UI_MAX_PROFILES : g_model.profile_count;
    if (0U == count)
    {
        draw_panel(8, 128, 240, 74);
        (void) draw_text(18, 144, "NO LEARNED IDS", 2U, RGB565_TEXT_DIM);
        (void) draw_text(36, 179, "START LEARN MODE", 1U, RGB565_BLUE);
        return;
    }

    for (uint8_t i = 0U; i < count; i++)
    {
        ui_id_profile_t const * const profile = &g_model.profiles[i];
        int16_t const y = (int16_t) (123 + ((int16_t) i * 40));
        fill_rect(8, y, 240, 36, (0U == (i & 1U)) ? RGB565_PANEL : RGB565_PANEL_ALT);
        fill_rect(8, (int16_t) (y + 35), 240, 1, RGB565_BORDER);

        (void) draw_hex(13, (int16_t) (y + 7), profile->id,
                        (0U != (profile->flags & UI_FRAME_FLAG_EXTENDED)) ? 8U : 3U,
                        1U, RGB565_CYAN);
        draw_frame_flags(13, (int16_t) (y + 21), profile->flags, RGB565_TEXT_DIM);
        (void) draw_u32(82, (int16_t) (y + 7), profile->dlc, 1U, 1U, RGB565_TEXT);
        (void) draw_fixed_one(106, (int16_t) (y + 7), profile->rate_hz_x10, 1U, RGB565_TEXT);

        int16_t x;
        if (profile->period_us >= 1000000U)
        {
            x = draw_fixed_one(151, (int16_t) (y + 7), profile->period_us / 100000U,
                               1U, RGB565_TEXT);
            (void) draw_text(x, (int16_t) (y + 7), "S", 1U, RGB565_TEXT_DIM);
        }
        else
        {
            x = draw_fixed_one(151, (int16_t) (y + 7), profile->period_us / 100U,
                               1U, RGB565_TEXT);
            (void) draw_text(x, (int16_t) (y + 7), "M", 1U, RGB565_TEXT_DIM);
        }

        (void) draw_u32(218, (int16_t) (y + 7), profile->anomaly_score / 10U,
                        1U, 1U, score_color(profile->anomaly_score));
        int16_t observed_x = draw_text(75, (int16_t) (y + 21), "N", 1U, RGB565_TEXT_DIM);
        (void) draw_u32(observed_x, (int16_t) (y + 21), profile->observed_frames,
                        1U, 1U, RGB565_TEXT_DIM);
    }
}

static void draw_live_frames(void)
{
    draw_panel(8, 94, 240, 25);
    (void) draw_text(13, 103, "AGE    ID       F DLC  DATA[0..7]", 1U, RGB565_TEXT_DIM);

    uint8_t const count = (g_model.live_frame_count > CAN_UI_MAX_LIVE_FRAMES) ?
                          CAN_UI_MAX_LIVE_FRAMES : g_model.live_frame_count;
    if (0U == count)
    {
        draw_panel(8, 128, 240, 54);
        (void) draw_text(38, 149, "WAITING FOR FRAMES", 1U, RGB565_TEXT_DIM);
        return;
    }

    for (uint8_t i = 0U; i < count; i++)
    {
        ui_live_frame_t const * const frame = &g_model.live_frames[i];
        int16_t const y = (int16_t) (122 + ((int16_t) i * 40));
        fill_rect(8, y, 240, 37, (0U == (i & 1U)) ? RGB565_PANEL : RGB565_PANEL_ALT);
        fill_rect(8, (int16_t) (y + 36), 240, 1, RGB565_BORDER);

        int16_t x;
        if (frame->age_ms >= 1000U)
        {
            x = draw_u32(13, (int16_t) (y + 6), frame->age_ms / 1000U, 1U, 1U, RGB565_TEXT_DIM);
            (void) draw_text(x, (int16_t) (y + 6), "S", 1U, RGB565_TEXT_DIM);
        }
        else
        {
            x = draw_u32(13, (int16_t) (y + 6), frame->age_ms, 1U, 1U, RGB565_TEXT_DIM);
            (void) draw_text(x, (int16_t) (y + 6), "M", 1U, RGB565_TEXT_DIM);
        }
        (void) draw_hex(50, (int16_t) (y + 6), frame->id,
                        (0U != (frame->flags & UI_FRAME_FLAG_EXTENDED)) ? 8U : 3U,
                        1U, RGB565_CYAN);
        draw_frame_flags(111, (int16_t) (y + 6), frame->flags, RGB565_AMBER);
        (void) draw_u32(139, (int16_t) (y + 6), frame->dlc, 1U, 1U, RGB565_TEXT);

        x = 13;
        uint8_t const bytes = (frame->dlc < CAN_UI_DATA_PREVIEW) ? frame->dlc : CAN_UI_DATA_PREVIEW;
        for (uint8_t byte = 0U; byte < bytes; byte++)
        {
            x = draw_hex(x, (int16_t) (y + 21), frame->data[byte], 2U, 1U, RGB565_TEXT);
            x = (int16_t) (x + 4);
        }
        if (0U == bytes)
        {
            x = draw_text(x, (int16_t) (y + 21), "NO DATA", 1U, RGB565_TEXT_DIM);
        }
        draw_reason_tokens((x < 174) ? 174 : x, (int16_t) (y + 21), frame->reason_mask,
                           243, score_color(frame->anomaly_score));
    }
}

static void draw_events(void)
{
    draw_panel(8, 94, 240, 25);
    (void) draw_text(13, 103, "TIME       ID          RISK / REASON", 1U, RGB565_TEXT_DIM);

    uint8_t const count = (g_model.event_count > CAN_UI_MAX_EVENTS) ?
                          CAN_UI_MAX_EVENTS : g_model.event_count;
    if (0U == count)
    {
        draw_panel(8, 128, 240, 54);
        (void) draw_text(53, 149, "NO IDS EVENTS", 1U, RGB565_GREEN);
        return;
    }

    for (uint8_t i = 0U; i < count; i++)
    {
        ui_event_t const * const event = &g_model.events[i];
        int16_t const y = (int16_t) (122 + ((int16_t) i * 40));
        fill_rect(8, y, 240, 37, (0U == (i & 1U)) ? RGB565_PANEL : RGB565_PANEL_ALT);
        fill_rect(8, (int16_t) (y + 36), 240, 1, RGB565_BORDER);

        int16_t x = draw_text(13, (int16_t) (y + 6), "T+", 1U, RGB565_TEXT_DIM);
        x = draw_u32(x, (int16_t) (y + 6), event->timestamp_ms / 1000U, 1U, 1U, RGB565_TEXT);
        (void) draw_text(x, (int16_t) (y + 6), "S", 1U, RGB565_TEXT_DIM);
        (void) draw_hex(80, (int16_t) (y + 6), event->id,
                        (0U != (event->flags & UI_FRAME_FLAG_EXTENDED)) ? 8U : 3U,
                        1U, RGB565_CYAN);
        (void) draw_u32(211, (int16_t) (y + 6), event->anomaly_score / 10U,
                        1U, 1U, score_color(event->anomaly_score));

        draw_reason_tokens(13, (int16_t) (y + 21), event->reason_mask, 243,
                           score_color(event->anomaly_score));
    }
}

bool can_ui_init(void)
{
    memset(&g_model, 0, sizeof(g_model));
    g_model.state = UI_SYSTEM_STATE_BOOT;
    g_model.nominal_bitrate_kbps = 500U;
    g_model.data_bitrate_kbps = 2000U;
    g_model.can_fd_enabled = true;
    g_page = UI_PAGE_DASHBOARD;
    g_dirty = true;
    memset(g_load_history, 0, sizeof(g_load_history));
    memset(g_score_history, 0, sizeof(g_score_history));
    g_history_count = 0U;
    g_history_head = 0U;
    g_last_history_ms = 0U;

    if ((!display_port_ready()) && (!display_port_init()))
    {
        g_model.state = UI_SYSTEM_STATE_DISPLAY_ERROR;
        g_initialized = false;
        return false;
    }

    g_initialized = true;
    return can_ui_render_if_dirty();
}

void can_ui_set_model(ui_view_model_t const * p_model)
{
    if (NULL != p_model)
    {
        history_push(p_model);
        g_model = *p_model;
        g_dirty = true;
    }
}

void can_ui_next_page(void)
{
    g_page = (ui_page_t) (((uint32_t) g_page + 1U) % (uint32_t) UI_PAGE_COUNT);
    g_dirty = true;
}

void can_ui_mark_dirty(void)
{
    g_dirty = true;
}

bool can_ui_render_if_dirty(void)
{
    if ((!g_initialized) || (!display_port_ready()))
    {
        return false;
    }
    if (!g_dirty)
    {
        return true;
    }

    fill_rect(0, 0, UI_WIDTH, UI_HEIGHT, RGB565_BACKGROUND);
    draw_header();
    switch (g_page)
    {
        case UI_PAGE_SYSTEM_HEALTH:
            draw_system_health();
            break;
        case UI_PAGE_ID_PROFILES:
            draw_profiles();
            break;
        case UI_PAGE_LIVE_FRAMES:
            draw_live_frames();
            break;
        case UI_PAGE_EVENTS:
            draw_events();
            break;
        case UI_PAGE_DASHBOARD:
        default:
            draw_dashboard();
            break;
    }
    draw_footer();
    display_port_keep_awake();
    display_port_flush();
    g_dirty = false;
    return true;
}
