#ifndef TUMOVGM_BRIDGE_UART_H
#define TUMOVGM_BRIDGE_UART_H

#include <stdint.h>

void tumovgm_bridge_uart_init(void);
void tumovgm_bridge_uart_poll(uint32_t now_ms);

#endif
