#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_PORT_WIDTH  (256U)
#define DISPLAY_PORT_HEIGHT (480U)

bool display_port_init(void);
bool display_port_ready(void);
uint16_t * display_port_framebuffer(void);
void display_port_flush(void);
/* Keep the panel/backlight in the explicit ON state.  There is deliberately
 * no display-idle timeout in the vehicle demonstrator. */
void display_port_keep_awake(void);

#endif /* DISPLAY_PORT_H */
