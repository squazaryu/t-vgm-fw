#ifndef TUMOVGM_BRIDGE_UART_H
#define TUMOVGM_BRIDGE_UART_H

#include <stdbool.h>
#include <stdint.h>

typedef struct TumovgmBridgeUartStatus {
    uint32_t session_id;
    uint32_t last_activity_ms;
    uint32_t received_frames;
    uint16_t last_error;
    uint8_t peer_major;
    bool negotiated;
    bool session_active;
} TumovgmBridgeUartStatus;

void tumovgm_bridge_uart_init(void);
void tumovgm_bridge_uart_poll(uint32_t now_ms);
void tumovgm_bridge_uart_get_status(TumovgmBridgeUartStatus* status);

#endif
