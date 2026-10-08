#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hal_data.h"

#define CONSOLE_RX_CAPACITY       (128U) /* Power-of-two ring: absorbs bursty host commands. */
#define CONSOLE_RX_INDEX_MASK     (CONSOLE_RX_CAPACITY - 1U)
#define CONSOLE_TX_BUFFER_SIZE    (256U)
#define CONSOLE_TX_TIMEOUT_LOOPS  (8000000UL)

static volatile bool g_console_tx_complete;
static volatile uint8_t g_console_rx_head;
static volatile uint8_t g_console_rx_tail;
static volatile uint32_t g_console_rx_dropped;
static volatile uint32_t g_console_errors;
static uint8_t g_console_rx[CONSOLE_RX_CAPACITY];
static bool g_console_open;

bool console_init(void)
{
    g_console_rx_head = 0U;
    g_console_rx_tail = 0U;
    g_console_rx_dropped = 0U;
    g_console_errors = 0U;
    g_console_tx_complete = false;
    g_console_open = (FSP_SUCCESS == g_uart0.p_api->open(g_uart0.p_ctrl, g_uart0.p_cfg));
    return g_console_open;
}

bool console_write(char const * text)
{
    if ((!g_console_open) || (NULL == text))
    {
        return false;
    }

    size_t const length = strlen(text);
    if (0U == length)
    {
        return true;
    }

    g_console_tx_complete = false;
    if (FSP_SUCCESS != g_uart0.p_api->write(g_uart0.p_ctrl, (uint8_t const *) text, (uint32_t) length))
    {
        return false;
    }

    uint32_t timeout = CONSOLE_TX_TIMEOUT_LOOPS;
    while ((!g_console_tx_complete) && (timeout > 0U))
    {
        timeout--;
        __NOP();
    }
    return (0U != timeout);
}

bool console_printf(char const * format, ...)
{
    if (NULL == format)
    {
        return false;
    }

    char buffer[CONSOLE_TX_BUFFER_SIZE];
    va_list arguments;
    va_start(arguments, format);
    int const length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0)
    {
        return false;
    }
    buffer[sizeof(buffer) - 1U] = '\0';
    return console_write(buffer);
}

bool console_read_char(uint8_t * value)
{
    if ((NULL == value) || (g_console_rx_tail == g_console_rx_head))
    {
        return false;
    }

    uint8_t const tail = g_console_rx_tail;
    *value = g_console_rx[tail];
    g_console_rx_tail = (uint8_t) ((tail + 1U) & CONSOLE_RX_INDEX_MASK);
    return true;
}

uint32_t console_rx_dropped(void)
{
    return g_console_rx_dropped;
}

uint32_t console_error_count(void)
{
    return g_console_errors;
}

void uart3_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        g_console_tx_complete = true;
    }
    else if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t const head = g_console_rx_head;
        uint8_t const next = (uint8_t) ((head + 1U) & CONSOLE_RX_INDEX_MASK);
        if (next == g_console_rx_tail)
        {
            g_console_rx_dropped++;
        }
        else
        {
            g_console_rx[head] = (uint8_t) p_args->data;
            g_console_rx_head = next;
        }
    }
    else if ((UART_EVENT_ERR_PARITY == p_args->event) ||
             (UART_EVENT_ERR_FRAMING == p_args->event) ||
             (UART_EVENT_ERR_OVERFLOW == p_args->event) ||
             (UART_EVENT_BREAK_DETECT == p_args->event))
    {
        g_console_errors++;
    }
}
