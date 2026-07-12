#include "bridge_uart.h"

#include <string.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <tumovgm/bridge.h>

#ifndef TUMOVGM_VERSION
#error "TUMOVGM_VERSION must be defined by the build system"
#endif

#ifndef TUMOVGM_GIT_COMMIT
#error "TUMOVGM_GIT_COMMIT must be defined by the build system"
#endif

#ifndef TUMOVGM_GIT_DIRTY
#error "TUMOVGM_GIT_DIRTY must be defined by the build system"
#endif

enum {
    TumovgmBridgeUartBaud = 230400,
    TumovgmBridgeUartTxPin = 0,
    TumovgmBridgeUartRxPin = 1,
};

typedef struct TumovgmBridgeUart {
    TumovgmBridge bridge;
    uint8_t rx_buffer[TumovgmFrameMaxSize];
    uint8_t tx_buffer[TumovgmFrameMaxSize];
    size_t rx_size;
    uint32_t received_frames;
    uint16_t last_error;
    uint8_t peer_major;
} TumovgmBridgeUart;

static TumovgmBridgeUart tumovgm_bridge_uart;

static uint16_t tumovgm_bridge_uart_read_u16(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void tumovgm_bridge_uart_discard(size_t count) {
    if(count >= tumovgm_bridge_uart.rx_size) {
        tumovgm_bridge_uart.rx_size = 0;
        return;
    }
    memmove(
        tumovgm_bridge_uart.rx_buffer,
        tumovgm_bridge_uart.rx_buffer + count,
        tumovgm_bridge_uart.rx_size - count);
    tumovgm_bridge_uart.rx_size -= count;
}

static void tumovgm_bridge_uart_process(uint32_t now_ms) {
    while(tumovgm_bridge_uart.rx_size > 0) {
        TumovgmFrame request;
        size_t consumed = 0;
        const TumovgmCodecStatus status = tumovgm_frame_decode(
            tumovgm_bridge_uart.rx_buffer,
            tumovgm_bridge_uart.rx_size,
            &request,
            &consumed);
        if(status == TumovgmCodecStatusNeedMore) return;

        if(status == TumovgmCodecStatusOk) {
            tumovgm_bridge_uart.received_frames++;
            tumovgm_bridge_uart.peer_major = request.major;
            TumovgmFrame response;
            if(tumovgm_bridge_handle(
                   &tumovgm_bridge_uart.bridge, &request, now_ms, &response)) {
                if(response.kind == TumovgmFrameKindError &&
                   response.payload_length >= TumovgmErrorPayloadSize) {
                    tumovgm_bridge_uart.last_error =
                        tumovgm_bridge_uart_read_u16(response.payload);
                } else if(request.message == TumovgmMessageHello) {
                    tumovgm_bridge_uart.last_error = 0;
                }
                size_t encoded_size = 0;
                if(tumovgm_frame_encode(
                       &response,
                       tumovgm_bridge_uart.tx_buffer,
                       sizeof(tumovgm_bridge_uart.tx_buffer),
                       &encoded_size) == TumovgmCodecStatusOk) {
                    uart_write_blocking(uart0, tumovgm_bridge_uart.tx_buffer, encoded_size);
                }
            }
        }

        tumovgm_bridge_uart_discard(consumed > 0 ? consumed : 1);
    }
}

void tumovgm_bridge_uart_init(void) {
    memset(&tumovgm_bridge_uart, 0, sizeof(tumovgm_bridge_uart));
    const TumovgmBridgeIdentity identity = {
        .firmware_version = TUMOVGM_VERSION,
        .git_commit = TUMOVGM_GIT_COMMIT,
        .hardware_target = TumovgmHardwareTargetVgmRp2040,
        .hardware_revision = 0,
        .dirty = TUMOVGM_GIT_DIRTY != 0,
    };
    tumovgm_bridge_init(
        &tumovgm_bridge_uart.bridge,
        &identity,
        UINT64_C(1) << TumovgmCapabilityBitVideoOut);

    uart_init(uart0, TumovgmBridgeUartBaud);
    gpio_set_function(TumovgmBridgeUartTxPin, GPIO_FUNC_UART);
    gpio_set_function(TumovgmBridgeUartRxPin, GPIO_FUNC_UART);
    gpio_pull_up(TumovgmBridgeUartRxPin);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart0, false, false);
    uart_set_fifo_enabled(uart0, true);
}

void tumovgm_bridge_uart_poll(uint32_t now_ms) {
    while(uart_is_readable(uart0)) {
        if(tumovgm_bridge_uart.rx_size == sizeof(tumovgm_bridge_uart.rx_buffer)) {
            tumovgm_bridge_uart_discard(1);
        }
        tumovgm_bridge_uart.rx_buffer[tumovgm_bridge_uart.rx_size++] =
            (uint8_t)uart_getc(uart0);
        tumovgm_bridge_uart_process(now_ms);
    }
    tumovgm_bridge_tick(&tumovgm_bridge_uart.bridge, now_ms);
}

void tumovgm_bridge_uart_get_status(TumovgmBridgeUartStatus* status) {
    if(status == NULL) return;
    *status = (TumovgmBridgeUartStatus){
        .session_id = tumovgm_bridge_uart.bridge.session.active_id,
        .last_activity_ms = tumovgm_bridge_uart.bridge.last_activity_ms,
        .received_frames = tumovgm_bridge_uart.received_frames,
        .last_error = tumovgm_bridge_uart.last_error,
        .peer_major = tumovgm_bridge_uart.peer_major,
        .negotiated = tumovgm_bridge_uart.bridge.negotiated_ready,
        .session_active =
            tumovgm_bridge_uart.bridge.session.state == TumovgmSessionStateActive,
    };
}
