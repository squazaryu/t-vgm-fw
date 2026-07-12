#include <tumovgm/bridge.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if(!(condition)) {                                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while(false)

static uint16_t read_u16(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* data, uint32_t value) {
    for(uint8_t index = 0; index < 4; index++)
        data[index] = (uint8_t)(value >> (index * 8));
}

static void write_u64(uint8_t* data, uint64_t value) {
    for(uint8_t index = 0; index < 8; index++)
        data[index] = (uint8_t)(value >> (index * 8));
}

static TumovgmFrame request(
    uint8_t major,
    uint8_t minor,
    uint16_t sequence,
    uint16_t message,
    const uint8_t* payload,
    uint16_t payload_length) {
    return (TumovgmFrame){
        .major = major,
        .minor = minor,
        .kind = TumovgmFrameKindRequest,
        .sequence = sequence,
        .message = message,
        .payload = payload,
        .payload_length = payload_length,
    };
}

static void make_hello(uint8_t* payload, uint16_t max_payload, uint64_t capabilities) {
    memset(payload, 0, TumovgmHelloPayloadSize);
    payload[0] = TumovgmRoleFlipper;
    write_u16(payload + 2, max_payload);
    write_u64(payload + 4, capabilities);
}

static bool response_is_error(const TumovgmFrame* response, TumovgmError error) {
    return response->kind == TumovgmFrameKindError &&
           response->payload_length == TumovgmErrorPayloadSize &&
           read_u16(response->payload) == (uint16_t)error;
}

static TumovgmBridge make_bridge(uint64_t capabilities) {
    TumovgmBridge bridge;
    static const TumovgmBridgeIdentity identity = {
        .firmware_version = "t-vgm-dev-001-004",
        .git_commit = "0123456789ab",
        .hardware_target = TumovgmHardwareTargetVgmRp2040,
        .hardware_revision = 0,
        .dirty = false,
    };
    tumovgm_bridge_init(&bridge, &identity, capabilities);
    return bridge;
}

typedef struct ExtensionContext {
    uint16_t calls;
    uint32_t last_now_ms;
} ExtensionContext;

static bool extension_handler(
    void* context,
    TumovgmBridge* bridge,
    const TumovgmFrame* request_frame,
    uint32_t now_ms,
    TumovgmFrame* response) {
    ExtensionContext* extension = context;
    extension->calls++;
    extension->last_now_ms = now_ms;
    if(request_frame->message != TumovgmMessageImuInfo) return false;
    bridge->response_payload[0] = 0x47;
    *response = (TumovgmFrame){
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = bridge->negotiated.minor,
        .kind = TumovgmFrameKindResponse,
        .sequence = request_frame->sequence,
        .message = request_frame->message,
        .payload_length = 1,
        .payload = bridge->response_payload,
    };
    return true;
}

static bool negotiate(TumovgmBridge* bridge, uint8_t peer_minor, TumovgmFrame* response) {
    uint8_t payload[TumovgmHelloPayloadSize];
    make_hello(payload, TUMOVGM_PROTOCOL_MAX_PAYLOAD, 0);
    const TumovgmFrame hello = request(
        TUMOVGM_PROTOCOL_MAJOR, peer_minor, 1, TumovgmMessageHello, payload, sizeof(payload));
    return tumovgm_bridge_handle(bridge, &hello, 100, response);
}

static bool test_negotiation_and_identity(void) {
    TumovgmBridge bridge = make_bridge(UINT64_C(3));
    TumovgmFrame response;
    CHECK(negotiate(&bridge, TUMOVGM_PROTOCOL_MINOR, &response));
    CHECK(response.kind == TumovgmFrameKindResponse);
    CHECK(response.message == TumovgmMessageHello);
    CHECK(response.payload_length == TumovgmHelloPayloadSize);
    CHECK(response.payload[0] == TumovgmRoleVgm);
    CHECK(response.payload[1] == TUMOVGM_PROTOCOL_MINOR);
    CHECK(read_u16(response.payload + 2) == TUMOVGM_PROTOCOL_MAX_PAYLOAD);
    CHECK(bridge.session.state == TumovgmSessionStateReady);

    const TumovgmFrame info = request(
        TUMOVGM_PROTOCOL_MAJOR, TUMOVGM_PROTOCOL_MINOR, 2, TumovgmMessageDeviceInfo, NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &info, 110, &response));
    CHECK(response.payload_length == TumovgmDeviceInfoPayloadSize);
    CHECK(read_u16(response.payload) == TumovgmHardwareTargetVgmRp2040);
    CHECK(memcmp(response.payload + 4, "t-vgm-dev-001-004", 17) == 0);
    CHECK(memcmp(response.payload + 28, "0123456789ab", 12) == 0);
    CHECK(response.payload[40] == 0);

    const TumovgmFrame capabilities = request(
        TUMOVGM_PROTOCOL_MAJOR, TUMOVGM_PROTOCOL_MINOR, 3, TumovgmMessageCapabilities, NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &capabilities, 120, &response));
    CHECK(response.payload_length == TumovgmCapabilitiesPayloadSize);
    CHECK(response.payload[0] == 3);
    return true;
}

static bool test_version_and_minor_compatibility(void) {
    TumovgmBridge bridge = make_bridge(0);
    uint8_t payload[TumovgmHelloPayloadSize];
    make_hello(payload, 128, 0);
    TumovgmFrame hello = request(2, 0, 7, TumovgmMessageHello, payload, sizeof(payload));
    TumovgmFrame response;
    CHECK(tumovgm_bridge_handle(&bridge, &hello, 0, &response));
    CHECK(response_is_error(&response, TumovgmErrorUnsupportedVersion));
    CHECK(!bridge.negotiated_ready);

    CHECK(negotiate(&bridge, 0, &response));
    CHECK(response.payload[1] == 0);
    const TumovgmFrame info = request(1, 0, 8, TumovgmMessageDeviceInfo, NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &info, 1, &response));
    CHECK(response_is_error(&response, TumovgmErrorUnsupportedMessage));
    return true;
}

static bool test_session_close_and_timeout(void) {
    TumovgmBridge bridge = make_bridge(UINT64_C(1));
    TumovgmFrame response;
    CHECK(negotiate(&bridge, 1, &response));

    uint8_t open_payload[TumovgmSessionOpenRequestSize] = {0};
    write_u64(open_payload, 1);
    write_u32(open_payload + 8, 1500);
    const TumovgmFrame open = request(1, 1, 2, TumovgmMessageSessionOpen, open_payload, 12);
    CHECK(tumovgm_bridge_handle(&bridge, &open, 1000, &response));
    CHECK(response.kind == TumovgmFrameKindResponse);
    const uint32_t session_id = read_u32(response.payload);
    CHECK(session_id != 0);
    CHECK(read_u32(response.payload + 12) == 1500);
    CHECK(bridge.session.state == TumovgmSessionStateActive);

    CHECK(tumovgm_bridge_handle(&bridge, &open, 1100, &response));
    CHECK(response_is_error(&response, TumovgmErrorBusy));

    uint8_t close_payload[TumovgmSessionClosePayloadSize];
    write_u32(close_payload, session_id + 1);
    TumovgmFrame close = request(1, 1, 3, TumovgmMessageSessionClose, close_payload, 4);
    CHECK(tumovgm_bridge_handle(&bridge, &close, 1200, &response));
    CHECK(response_is_error(&response, TumovgmErrorBadState));

    write_u32(close_payload, session_id);
    CHECK(tumovgm_bridge_handle(&bridge, &close, 1300, &response));
    CHECK(response.kind == TumovgmFrameKindResponse);
    CHECK(tumovgm_bridge_handle(&bridge, &close, 1301, &response));
    CHECK(response.kind == TumovgmFrameKindResponse);

    CHECK(tumovgm_bridge_handle(&bridge, &open, 2000, &response));
    CHECK(bridge.session.state == TumovgmSessionStateActive);
    CHECK(!tumovgm_bridge_tick(&bridge, 3499));
    CHECK(tumovgm_bridge_tick(&bridge, 3500));
    CHECK(bridge.session.state == TumovgmSessionStateReady);
    return true;
}

static bool test_errors_ping_and_disconnect(void) {
    TumovgmBridge bridge = make_bridge(0);
    TumovgmFrame response;
    const TumovgmFrame before_hello = request(1, 1, 1, TumovgmMessagePing, NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &before_hello, 0, &response));
    CHECK(response_is_error(&response, TumovgmErrorBadState));
    CHECK(negotiate(&bridge, 1, &response));

    const uint8_t ping_payload[TumovgmPingPayloadSize] = {0, 1, 2, 3, 4, 5, 6, 7};
    const TumovgmFrame ping = request(1, 1, 3, TumovgmMessagePing, ping_payload, 8);
    CHECK(tumovgm_bridge_handle(&bridge, &ping, 200, &response));
    CHECK(memcmp(response.payload, ping_payload, sizeof(ping_payload)) == 0);

    const TumovgmFrame unknown = request(1, 1, 4, UINT16_C(0x7FFF), NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &unknown, 201, &response));
    CHECK(response_is_error(&response, TumovgmErrorUnsupportedMessage));

    tumovgm_bridge_disconnect(&bridge);
    CHECK(bridge.session.state == TumovgmSessionStateDisconnected);
    CHECK(!bridge.negotiated_ready);
    return true;
}

static bool test_extension_dispatch(void) {
    TumovgmBridge bridge = make_bridge(UINT64_C(1) << TumovgmCapabilityBitImu);
    ExtensionContext extension = {0};
    tumovgm_bridge_set_extension_handler(&bridge, extension_handler, &extension);
    TumovgmFrame response;
    CHECK(negotiate(&bridge, TUMOVGM_PROTOCOL_MINOR, &response));

    const TumovgmFrame info =
        request(TUMOVGM_PROTOCOL_MAJOR, TUMOVGM_PROTOCOL_MINOR, 9, TumovgmMessageImuInfo, NULL, 0);
    CHECK(tumovgm_bridge_handle(&bridge, &info, 321, &response));
    CHECK(response.kind == TumovgmFrameKindResponse);
    CHECK(response.payload_length == 1);
    CHECK(response.payload[0] == 0x47);
    CHECK(extension.calls == 1);
    CHECK(extension.last_now_ms == 321);
    return true;
}

int main(void) {
    if(!test_negotiation_and_identity() || !test_version_and_minor_compatibility() ||
       !test_session_close_and_timeout() || !test_errors_ping_and_disconnect() ||
       !test_extension_dispatch()) {
        return 1;
    }
    puts("bridge_host_test: PASS");
    return 0;
}
