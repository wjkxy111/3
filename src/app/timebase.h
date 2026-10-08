#ifndef APP_TIMEBASE_H
#define APP_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

bool timebase_init(void);
void timebase_poll(void);
uint32_t timebase_now_us32(void);
uint64_t timebase_now_us64(void);

#endif /* APP_TIMEBASE_H */
