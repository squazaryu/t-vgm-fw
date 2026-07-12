#include "bridge_uart.h"

#include <string.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <tumovgm/bridge.h>
#include <tumovgm/imu_service.h>

#include "imu_icm42688.h"

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
    TumovgmImuInfoPayloadSize = 12,
    TumovgmImuConfigRequestSize = 8,
    TumovgmImuConfigResponseSize = 12,
    TumovgmStreamCreditPayloadSize = 8,
    TumovgmStreamCreditResponseSize = 4,
    TumovgmImuSamplePayloadSize = 28,
    TumovgmImuGesturePayloadSize = 16,
};

typedef struct TumovgmBridgeUart {
    TumovgmBridge bridge;
    uint8_t rx_buffer[TumovgmFrameMaxSize];
    uint8_t tx_buffer[TumovgmFrameMaxSize];
    size_t rx_size;
    uint32_t received_frames;
    uint16_t last_error;
    uint8_t peer_major;
    TumovgmIcm42688 imu_driver;
    TumovgmImuService imu;
    uint8_t imu_payload[TumovgmImuSamplePayloadSize];
} TumovgmBridgeUart;

static TumovgmBridgeUart tumovgm_bridge_uart;

static uint16_t tumovgm_bridge_uart_read_u16(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t tumovgm_bridge_uart_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void tumovgm_bridge_uart_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void tumovgm_bridge_uart_write_i16(uint8_t* data, int16_t value) {
    tumovgm_bridge_uart_write_u16(data, (uint16_t)value);
}

static void tumovgm_bridge_uart_write_u32(uint8_t* data, uint32_t value) {
    for(uint8_t index = 0; index < 4; index++)
        data[index] = (uint8_t)(value >> (index * 8));
}

static void tumovgm_bridge_uart_prepare_response(
    const TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response,
    TumovgmFrameKind kind,
    uint16_t payload_length) {
    *response = (TumovgmFrame){
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = bridge->negotiated.minor,
        .kind = kind,
        .sequence = request->sequence,
        .message = request->message,
        .payload_length = payload_length,
        .payload = payload_length == 0 ? NULL : tumovgm_bridge_uart.imu_payload,
    };
}

static bool tumovgm_bridge_uart_error(
    const TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response,
    TumovgmError error,
    uint16_t detail) {
    tumovgm_bridge_uart_write_u16(tumovgm_bridge_uart.imu_payload, (uint16_t)error);
    tumovgm_bridge_uart_write_u16(tumovgm_bridge_uart.imu_payload + 2, detail);
    tumovgm_bridge_uart_prepare_response(
        bridge, request, response, TumovgmFrameKindError, TumovgmErrorPayloadSize);
    return true;
}

static bool tumovgm_bridge_uart_imu_extension(
    void* context,
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response) {
    TumovgmBridgeUart* endpoint = context;
    if(bridge->negotiated.minor < 2) return false;

    if(request->message == TumovgmMessageImuInfo) {
        if(request->payload_length != 0) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorMalformed, request->payload_length);
        }
        memset(endpoint->imu_payload, 0, TumovgmImuInfoPayloadSize);
        endpoint->imu_payload[0] = (uint8_t)endpoint->imu.health;
        endpoint->imu_payload[1] = endpoint->imu.who_am_i;
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload + 2, endpoint->imu.bus_error);
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload + 4, TumovgmImuMaximumRateHz);
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload + 6, 0x0007);
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload + 8, 1);
        tumovgm_bridge_uart_prepare_response(
            bridge, request, response, TumovgmFrameKindResponse, TumovgmImuInfoPayloadSize);
        return true;
    }

    if(request->message == TumovgmMessageImuConfig) {
        if(request->payload_length != TumovgmImuConfigRequestSize) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorMalformed, request->payload_length);
        }
        if(bridge->session.state != TumovgmSessionStateActive ||
           (bridge->active_capabilities & (UINT64_C(1) << TumovgmCapabilityBitImu)) == 0) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorNoCapability, TumovgmCapabilityBitImu);
        }
        const uint32_t session_id = tumovgm_bridge_uart_read_u32(request->payload);
        const uint16_t requested_rate = tumovgm_bridge_uart_read_u16(request->payload + 4);
        const uint8_t flags = request->payload[6];
        if(session_id != bridge->session.active_id || request->payload[7] != 0 || flags == 0 ||
           (flags & (uint8_t)~TumovgmImuKnownFlags) != 0 ||
           requested_rate < TumovgmImuMinimumRateHz) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorMalformed, requested_rate);
        }
        uint16_t actual_rate = 0;
        if(!tumovgm_imu_service_configure(
               &endpoint->imu, session_id, requested_rate, flags, now_ms, &actual_rate)) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorInternal, endpoint->imu.bus_error);
        }
        memset(endpoint->imu_payload, 0, TumovgmImuConfigResponseSize);
        tumovgm_bridge_uart_write_u32(endpoint->imu_payload, session_id);
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload + 4, actual_rate);
        endpoint->imu_payload[6] = flags;
        endpoint->imu_payload[7] = TumovgmImuStreamId;
        tumovgm_bridge_uart_write_u32(endpoint->imu_payload + 8, 1000000U / actual_rate);
        tumovgm_bridge_uart_prepare_response(
            bridge, request, response, TumovgmFrameKindResponse, TumovgmImuConfigResponseSize);
        return true;
    }

    if(request->message == TumovgmMessageStreamCredit) {
        if(request->payload_length != TumovgmStreamCreditPayloadSize) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorMalformed, request->payload_length);
        }
        const uint32_t session_id = tumovgm_bridge_uart_read_u32(request->payload);
        const uint16_t stream_id = tumovgm_bridge_uart_read_u16(request->payload + 4);
        const uint16_t credits = tumovgm_bridge_uart_read_u16(request->payload + 6);
        if(stream_id != TumovgmImuStreamId) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorBadState, stream_id);
        }
        uint16_t total_credits = 0;
        if(!tumovgm_imu_service_grant(&endpoint->imu, session_id, credits, &total_credits)) {
            return tumovgm_bridge_uart_error(
                bridge, request, response, TumovgmErrorOverflow, credits);
        }
        memset(endpoint->imu_payload, 0, TumovgmStreamCreditResponseSize);
        tumovgm_bridge_uart_write_u16(endpoint->imu_payload, total_credits);
        tumovgm_bridge_uart_prepare_response(
            bridge, request, response, TumovgmFrameKindResponse, TumovgmStreamCreditResponseSize);
        return true;
    }

    return false;
}

static bool tumovgm_bridge_uart_send_frame(const TumovgmFrame* frame) {
    size_t encoded_size = 0;
    if(tumovgm_frame_encode(
           frame,
           tumovgm_bridge_uart.tx_buffer,
           sizeof(tumovgm_bridge_uart.tx_buffer),
           &encoded_size) != TumovgmCodecStatusOk) {
        return false;
    }
    uart_write_blocking(uart0, tumovgm_bridge_uart.tx_buffer, encoded_size);
    return true;
}

static void tumovgm_bridge_uart_send_imu_event(const TumovgmImuEvent* event) {
    uint8_t* const payload = tumovgm_bridge_uart.imu_payload;
    memset(payload, 0, sizeof(tumovgm_bridge_uart.imu_payload));
    TumovgmFrame frame = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = tumovgm_bridge_uart.bridge.negotiated.minor,
        .kind = TumovgmFrameKindEvent,
        .sequence = 0,
        .payload = payload,
    };
    if(event->kind == TumovgmImuEventSample) {
        const TumovgmImuSample* sample = &event->data.sample;
        frame.message = TumovgmMessageStreamData;
        frame.payload_length = TumovgmImuSamplePayloadSize;
        tumovgm_bridge_uart_write_u32(payload, sample->session_id);
        tumovgm_bridge_uart_write_u16(payload + 4, TumovgmImuStreamId);
        tumovgm_bridge_uart_write_u16(payload + 6, sample->sequence);
        tumovgm_bridge_uart_write_u32(payload + 8, sample->timestamp_ms);
        tumovgm_bridge_uart_write_i16(payload + 12, sample->temperature_centi_c);
        for(uint8_t axis = 0; axis < 3; axis++) {
            tumovgm_bridge_uart_write_i16(payload + 14 + axis * 2, sample->acceleration_mg[axis]);
            tumovgm_bridge_uart_write_i16(
                payload + 20 + axis * 2, sample->angular_velocity_deci_dps[axis]);
        }
        payload[26] = (uint8_t)sample->orientation;
        payload[27] = sample->flags;
    } else if(event->kind == TumovgmImuEventGesture) {
        const TumovgmImuGestureEvent* gesture = &event->data.gesture;
        frame.message = TumovgmMessageImuGesture;
        frame.payload_length = TumovgmImuGesturePayloadSize;
        tumovgm_bridge_uart_write_u32(payload, gesture->session_id);
        tumovgm_bridge_uart_write_u16(payload + 4, gesture->sequence);
        payload[6] = (uint8_t)gesture->gesture;
        payload[7] = gesture->confidence;
        tumovgm_bridge_uart_write_u32(payload + 8, gesture->timestamp_ms);
        payload[12] = (uint8_t)gesture->orientation;
    } else {
        return;
    }
    tumovgm_bridge_uart_send_frame(&frame);
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
            tumovgm_bridge_uart.rx_buffer, tumovgm_bridge_uart.rx_size, &request, &consumed);
        if(status == TumovgmCodecStatusNeedMore) return;

        if(status == TumovgmCodecStatusOk) {
            tumovgm_bridge_uart.received_frames++;
            tumovgm_bridge_uart.peer_major = request.major;
            TumovgmFrame response;
            if(tumovgm_bridge_handle(&tumovgm_bridge_uart.bridge, &request, now_ms, &response)) {
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
    tumovgm_icm42688_init(&tumovgm_bridge_uart.imu_driver);
    tumovgm_imu_service_init(
        &tumovgm_bridge_uart.imu, tumovgm_icm42688_driver(), &tumovgm_bridge_uart.imu_driver);
    const TumovgmBridgeIdentity identity = {
        .firmware_version = TUMOVGM_VERSION,
        .git_commit = TUMOVGM_GIT_COMMIT,
        .hardware_target = TumovgmHardwareTargetVgmRp2040,
        .hardware_revision = 0,
        .dirty = TUMOVGM_GIT_DIRTY != 0,
    };
    uint64_t capabilities = UINT64_C(1) << TumovgmCapabilityBitVideoOut;
    if(tumovgm_bridge_uart.imu.health == TumovgmImuHealthReady) {
        capabilities |= UINT64_C(1) << TumovgmCapabilityBitImu;
    }
    tumovgm_bridge_init(&tumovgm_bridge_uart.bridge, &identity, capabilities);
    tumovgm_bridge_set_extension_handler(
        &tumovgm_bridge_uart.bridge, tumovgm_bridge_uart_imu_extension, &tumovgm_bridge_uart);

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
        tumovgm_bridge_uart.rx_buffer[tumovgm_bridge_uart.rx_size++] = (uint8_t)uart_getc(uart0);
        tumovgm_bridge_uart_process(now_ms);
    }
    tumovgm_bridge_tick(&tumovgm_bridge_uart.bridge, now_ms);
    const bool session_active = tumovgm_bridge_uart.bridge.session.state ==
                                TumovgmSessionStateActive;
    tumovgm_imu_service_sync_session(
        &tumovgm_bridge_uart.imu, session_active, tumovgm_bridge_uart.bridge.session.active_id);
    TumovgmImuEvent event;
    if(tumovgm_imu_service_poll(&tumovgm_bridge_uart.imu, now_ms, &event)) {
        tumovgm_bridge_uart_send_imu_event(&event);
    }
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
        .session_active = tumovgm_bridge_uart.bridge.session.state == TumovgmSessionStateActive,
        .imu_available = tumovgm_bridge_uart.imu.who_am_i == TumovgmImuWhoAmI,
        .imu_healthy = tumovgm_bridge_uart.imu.health == TumovgmImuHealthReady ||
                       tumovgm_bridge_uart.imu.health == TumovgmImuHealthCalibrating,
    };
}
