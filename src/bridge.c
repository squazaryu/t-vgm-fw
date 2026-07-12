#include <tumovgm/bridge.h>

#include <string.h>

static uint16_t tumovgm_bridge_read_u16(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t tumovgm_bridge_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t tumovgm_bridge_read_u64(const uint8_t* data) {
    uint64_t value = 0;
    for(uint8_t index = 0; index < 8; index++) {
        value |= (uint64_t)data[index] << (index * 8);
    }
    return value;
}

static void tumovgm_bridge_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void tumovgm_bridge_write_u32(uint8_t* data, uint32_t value) {
    for(uint8_t index = 0; index < 4; index++) {
        data[index] = (uint8_t)(value >> (index * 8));
    }
}

static void tumovgm_bridge_write_u64(uint8_t* data, uint64_t value) {
    for(uint8_t index = 0; index < 8; index++) {
        data[index] = (uint8_t)(value >> (index * 8));
    }
}

static void tumovgm_bridge_copy_ascii(uint8_t* output, size_t size, const char* value) {
    memset(output, 0, size);
    if(value == NULL) return;
    for(size_t index = 0; index < size && value[index] != '\0'; index++) {
        const uint8_t character = (uint8_t)value[index];
        output[index] = (character >= 0x20 && character <= 0x7E) ? character : (uint8_t)'?';
    }
}

static void tumovgm_bridge_prepare_response(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response,
    TumovgmFrameKind kind,
    uint16_t payload_length) {
    *response = (TumovgmFrame){
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = bridge->negotiated_ready ? bridge->negotiated.minor : TUMOVGM_PROTOCOL_MINOR,
        .kind = kind,
        .sequence = request->sequence,
        .message = request->message,
        .payload_length = payload_length,
        .payload = payload_length == 0 ? NULL : bridge->response_payload,
    };
}

static bool tumovgm_bridge_error(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response,
    TumovgmError error,
    uint16_t detail) {
    tumovgm_bridge_write_u16(bridge->response_payload, (uint16_t)error);
    tumovgm_bridge_write_u16(bridge->response_payload + 2, detail);
    tumovgm_bridge_prepare_response(
        bridge, request, response, TumovgmFrameKindError, TumovgmErrorPayloadSize);
    return true;
}

static bool tumovgm_bridge_state_is_ready(const TumovgmBridge* bridge) {
    return bridge->negotiated_ready &&
           (bridge->session.state == TumovgmSessionStateReady ||
            bridge->session.state == TumovgmSessionStateActive);
}

static bool tumovgm_bridge_handle_hello(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response) {
    if(request->payload_length != TumovgmHelloPayloadSize ||
       request->payload[0] != TumovgmRoleFlipper || request->payload[1] != 0) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }

    const uint16_t peer_max_payload = tumovgm_bridge_read_u16(request->payload + 2);
    const TumovgmNegotiationStatus status = tumovgm_protocol_negotiate(
        request->major, request->minor, peer_max_payload, &bridge->negotiated);
    if(status == TumovgmNegotiationStatusMajorMismatch) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorUnsupportedVersion, request->major);
    }
    if(status != TumovgmNegotiationStatusOk) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, peer_max_payload);
    }

    tumovgm_session_disconnect(&bridge->session);
    tumovgm_session_init(&bridge->session);
    tumovgm_session_connect(&bridge->session);
    tumovgm_session_ready(&bridge->session);
    bridge->negotiated_ready = true;
    bridge->active_capabilities = 0;
    bridge->last_activity_ms = now_ms;

    memset(bridge->response_payload, 0, TumovgmHelloPayloadSize);
    bridge->response_payload[0] = TumovgmRoleVgm;
    bridge->response_payload[1] = bridge->negotiated.minor;
    tumovgm_bridge_write_u16(bridge->response_payload + 2, bridge->negotiated.max_payload);
    tumovgm_bridge_write_u64(bridge->response_payload + 4, bridge->available_capabilities);
    tumovgm_bridge_prepare_response(
        bridge, request, response, TumovgmFrameKindResponse, TumovgmHelloPayloadSize);
    return true;
}

static bool tumovgm_bridge_handle_capabilities(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response) {
    if(request->payload_length != 0) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }
    memset(bridge->response_payload, 0, TumovgmCapabilitiesPayloadSize);
    tumovgm_bridge_write_u64(bridge->response_payload, bridge->available_capabilities);
    tumovgm_bridge_write_u64(bridge->response_payload + 8, bridge->active_capabilities);
    tumovgm_bridge_write_u16(bridge->response_payload + 16, bridge->negotiated.max_payload);
    tumovgm_bridge_prepare_response(
        bridge, request, response, TumovgmFrameKindResponse, TumovgmCapabilitiesPayloadSize);
    return true;
}

static bool tumovgm_bridge_handle_device_info(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response) {
    if(bridge->negotiated.minor < 1) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorUnsupportedMessage, request->message);
    }
    if(request->payload_length != 0) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }

    memset(bridge->response_payload, 0, TumovgmDeviceInfoPayloadSize);
    tumovgm_bridge_write_u16(bridge->response_payload, bridge->identity.hardware_target);
    tumovgm_bridge_write_u16(bridge->response_payload + 2, bridge->identity.hardware_revision);
    tumovgm_bridge_copy_ascii(
        bridge->response_payload + 4, 24, bridge->identity.firmware_version);
    tumovgm_bridge_copy_ascii(bridge->response_payload + 28, 12, bridge->identity.git_commit);
    bridge->response_payload[40] = bridge->identity.dirty ? 1 : 0;
    tumovgm_bridge_prepare_response(
        bridge, request, response, TumovgmFrameKindResponse, TumovgmDeviceInfoPayloadSize);
    return true;
}

static bool tumovgm_bridge_handle_session_open(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response) {
    if(request->payload_length != TumovgmSessionOpenRequestSize) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }
    if(bridge->session.state != TumovgmSessionStateReady) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorBusy, bridge->session.state);
    }

    const uint64_t requested_capabilities = tumovgm_bridge_read_u64(request->payload);
    const uint64_t unsupported = requested_capabilities & ~bridge->available_capabilities;
    if(unsupported != 0) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorNoCapability, (uint16_t)unsupported);
    }
    uint32_t lease_ms = tumovgm_bridge_read_u32(request->payload + 8);
    if(lease_ms == 0) lease_ms = TumovgmDefaultLeaseMs;
    if(lease_ms < TumovgmMinimumLeaseMs) lease_ms = TumovgmMinimumLeaseMs;
    if(lease_ms > TumovgmMaximumLeaseMs) lease_ms = TumovgmMaximumLeaseMs;

    bridge->next_session_id++;
    if(bridge->next_session_id == 0) bridge->next_session_id++;
    if(tumovgm_session_open(&bridge->session, bridge->next_session_id) !=
       TumovgmSessionResultOk) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorInternal, bridge->session.state);
    }
    bridge->active_capabilities = requested_capabilities;
    bridge->lease_ms = lease_ms;
    bridge->last_activity_ms = now_ms;

    memset(bridge->response_payload, 0, TumovgmSessionOpenResponseSize);
    tumovgm_bridge_write_u32(bridge->response_payload, bridge->session.active_id);
    tumovgm_bridge_write_u64(bridge->response_payload + 4, bridge->active_capabilities);
    tumovgm_bridge_write_u32(bridge->response_payload + 12, bridge->lease_ms);
    tumovgm_bridge_prepare_response(
        bridge, request, response, TumovgmFrameKindResponse, TumovgmSessionOpenResponseSize);
    return true;
}

static bool tumovgm_bridge_handle_session_close(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response) {
    if(request->payload_length != TumovgmSessionClosePayloadSize) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }
    const uint32_t session_id = tumovgm_bridge_read_u32(request->payload);
    const TumovgmSessionResult result = tumovgm_session_close(&bridge->session, session_id);
    if(result != TumovgmSessionResultOk && result != TumovgmSessionResultAlreadyClosed) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorBadState, (uint16_t)result);
    }
    bridge->active_capabilities = 0;
    tumovgm_bridge_prepare_response(bridge, request, response, TumovgmFrameKindResponse, 0);
    return true;
}

static bool tumovgm_bridge_handle_cancel(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    TumovgmFrame* response) {
    if(request->payload_length != TumovgmCancelPayloadSize) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorMalformed, request->payload_length);
    }
    const uint32_t session_id = tumovgm_bridge_read_u32(request->payload);
    if(session_id != bridge->session.active_id && session_id != bridge->session.last_closed_id) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorBadState, (uint16_t)session_id);
    }
    tumovgm_bridge_prepare_response(bridge, request, response, TumovgmFrameKindResponse, 0);
    return true;
}

void tumovgm_bridge_init(
    TumovgmBridge* bridge,
    const TumovgmBridgeIdentity* identity,
    uint64_t available_capabilities) {
    memset(bridge, 0, sizeof(*bridge));
    bridge->identity = *identity;
    bridge->available_capabilities = available_capabilities;
    tumovgm_session_init(&bridge->session);
}

bool tumovgm_bridge_handle(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response) {
    if(bridge == NULL || request == NULL || response == NULL) return false;
    if(request->kind != TumovgmFrameKindRequest) return false;
    if(request->major != TUMOVGM_PROTOCOL_MAJOR) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorUnsupportedVersion, request->major);
    }
    if(request->message == TumovgmMessageHello) {
        return tumovgm_bridge_handle_hello(bridge, request, now_ms, response);
    }
    if(!tumovgm_bridge_state_is_ready(bridge)) {
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorBadState, bridge->session.state);
    }

    bridge->last_activity_ms = now_ms;
    switch(request->message) {
    case TumovgmMessageCapabilities:
        return tumovgm_bridge_handle_capabilities(bridge, request, response);
    case TumovgmMessageDeviceInfo:
        return tumovgm_bridge_handle_device_info(bridge, request, response);
    case TumovgmMessageSessionOpen:
        return tumovgm_bridge_handle_session_open(bridge, request, now_ms, response);
    case TumovgmMessageSessionClose:
        return tumovgm_bridge_handle_session_close(bridge, request, response);
    case TumovgmMessageCancel:
        return tumovgm_bridge_handle_cancel(bridge, request, response);
    case TumovgmMessagePing:
        if(request->payload_length != TumovgmPingPayloadSize) {
            return tumovgm_bridge_error(
                bridge, request, response, TumovgmErrorMalformed, request->payload_length);
        }
        memcpy(bridge->response_payload, request->payload, TumovgmPingPayloadSize);
        tumovgm_bridge_prepare_response(
            bridge, request, response, TumovgmFrameKindResponse, TumovgmPingPayloadSize);
        return true;
    default:
        return tumovgm_bridge_error(
            bridge, request, response, TumovgmErrorUnsupportedMessage, request->message);
    }
}

bool tumovgm_bridge_tick(TumovgmBridge* bridge, uint32_t now_ms) {
    if(bridge == NULL || bridge->session.state != TumovgmSessionStateActive) return false;
    if((uint32_t)(now_ms - bridge->last_activity_ms) < bridge->lease_ms) return false;
    const uint32_t session_id = bridge->session.active_id;
    tumovgm_session_close(&bridge->session, session_id);
    bridge->active_capabilities = 0;
    return true;
}

void tumovgm_bridge_disconnect(TumovgmBridge* bridge) {
    if(bridge == NULL) return;
    tumovgm_session_disconnect(&bridge->session);
    bridge->active_capabilities = 0;
    bridge->negotiated_ready = false;
}
