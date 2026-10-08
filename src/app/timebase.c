#include "timebase.h"

#include "hal_data.h"

static volatile uint32_t g_timebase_sequence;
static volatile uint32_t g_timebase_epoch;
static volatile uint32_t g_timebase_last_cycles;
static uint32_t g_cycles_per_microsecond;
static bool g_timebase_ready;

bool timebase_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t const before = DWT->CYCCNT;
    for (volatile uint32_t i = 0U; i < 64U; i++)
    {
        __NOP();
    }

    g_timebase_ready = (DWT->CYCCNT != before) && (SystemCoreClock >= 1000000U);
    g_cycles_per_microsecond = SystemCoreClock / 1000000U;
    g_timebase_sequence = 0U;
    g_timebase_epoch = 0U;
    g_timebase_last_cycles = DWT->CYCCNT;
    return g_timebase_ready;
}

void timebase_poll(void)
{
    if (!g_timebase_ready)
    {
        return;
    }

    uint32_t const primask = __get_PRIMASK();
    __disable_irq();
    g_timebase_sequence++;
    __DMB();

    uint32_t const now = DWT->CYCCNT;
    if (now < g_timebase_last_cycles)
    {
        g_timebase_epoch++;
    }
    g_timebase_last_cycles = now;

    __DMB();
    g_timebase_sequence++;
    if (0U == primask)
    {
        __enable_irq();
    }
}

static uint64_t timebase_cycles64(void)
{
    if (!g_timebase_ready)
    {
        return 0U;
    }

    uint32_t sequence_before;
    uint32_t sequence_after;
    uint32_t epoch;
    uint32_t last;
    uint32_t now;

    do
    {
        sequence_before = g_timebase_sequence;
        __DMB();
        epoch = g_timebase_epoch;
        last = g_timebase_last_cycles;
        now = DWT->CYCCNT;
        __DMB();
        sequence_after = g_timebase_sequence;
    } while ((sequence_before != sequence_after) || (0U != (sequence_before & 1U)));

    /* An ISR can observe the counter just after wrap but before the main loop polls it. */
    if (now < last)
    {
        epoch++;
    }
    return (((uint64_t) epoch) << 32) | now;
}

uint64_t timebase_now_us64(void)
{
    if (0U == g_cycles_per_microsecond)
    {
        return 0U;
    }
    return timebase_cycles64() / g_cycles_per_microsecond;
}

uint32_t timebase_now_us32(void)
{
    return (uint32_t) timebase_now_us64();
}
