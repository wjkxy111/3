#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

bool console_init(void);
bool console_write(char const * text);
bool console_printf(char const * format, ...);
bool console_read_char(uint8_t * value);
uint32_t console_rx_dropped(void);
uint32_t console_error_count(void);

#endif /* APP_CONSOLE_H */
