#ifndef TUMOVGM_BRIDGE_H
#define TUMOVGM_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include <tumovgm/protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TumovgmHelloPayloadSize = 12,
    TumovgmCapabilitiesPayloadSize = 24,
    TumovgmDeviceInfoPayloadSize = 48,
    TumovgmSessionOpenRequestSize = 12,
    TumovgmSessionOpenResponseSize = 16,
    TumovgmSessionClosePayloadSize = 4,
    TumovgmCancelPayloadSize = 8,
    TumovgmPingPayloadSize = 8,
    TumovgmErrorPayloadSize = 4,
    TumovgmDefaultLeaseMs = 3000,
    TumovgmMinimumLeaseMs = 1000,
    TumovgmMaximumLeaseMs = 10000,
};

typedef struct TumovgmBridgeIdentity {
    const char* firmware_version;
    const char* git_commit;
    uint16_t hardware_target;
    uint16_t hardware_revision;
    bool dirty;
} TumovgmBridgeIdentity;

struct TumovgmBridge;
typedef bool (*TumovgmBridgeExtensionHandler)(
    void* context,
    struct TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response);

typedef struct TumovgmBridge {
    TumovgmSession session;
    TumovgmBridgeIdentity identity;
    TumovgmNegotiatedProtocol negotiated;
    uint64_t available_capabilities;
    uint64_t active_capabilities;
    uint32_t next_session_id;
    uint32_t lease_ms;
    uint32_t last_activity_ms;
    bool negotiated_ready;
    TumovgmBridgeExtensionHandler extension_handler;
    void* extension_context;
    uint8_t response_payload[TUMOVGM_PROTOCOL_MAX_PAYLOAD];
} TumovgmBridge;

void tumovgm_bridge_init(
    TumovgmBridge* bridge,
    const TumovgmBridgeIdentity* identity,
    uint64_t available_capabilities);

void tumovgm_bridge_set_extension_handler(
    TumovgmBridge* bridge,
    TumovgmBridgeExtensionHandler handler,
    void* context);

bool tumovgm_bridge_handle(
    TumovgmBridge* bridge,
    const TumovgmFrame* request,
    uint32_t now_ms,
    TumovgmFrame* response);

bool tumovgm_bridge_tick(TumovgmBridge* bridge, uint32_t now_ms);
void tumovgm_bridge_disconnect(TumovgmBridge* bridge);

#ifdef __cplusplus
}
#endif

#endif
