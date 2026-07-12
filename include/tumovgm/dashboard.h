#ifndef TUMOVGM_DASHBOARD_H
#define TUMOVGM_DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TumovgmDashboardWidth = 320,
    TumovgmDashboardHeight = 240,
    TumovgmDashboardPixelsPerByte = 2,
    TumovgmDashboardBufferSize =
        TumovgmDashboardWidth * TumovgmDashboardHeight / TumovgmDashboardPixelsPerByte,
};

typedef enum TumovgmDashboardColor {
    TumovgmDashboardColorBackground = 0,
    TumovgmDashboardColorSurface = 1,
    TumovgmDashboardColorOrange = 2,
    TumovgmDashboardColorText = 3,
    TumovgmDashboardColorMuted = 4,
    TumovgmDashboardColorGreen = 5,
    TumovgmDashboardColorRed = 6,
    TumovgmDashboardColorYellow = 7,
    TumovgmDashboardColorBlack = 8,
} TumovgmDashboardColor;

typedef enum TumovgmDashboardLinkState {
    TumovgmDashboardLinkWaiting = 0,
    TumovgmDashboardLinkConnected,
    TumovgmDashboardLinkActive,
    TumovgmDashboardLinkIncompatible,
    TumovgmDashboardLinkError,
} TumovgmDashboardLinkState;

typedef struct TumovgmDashboardFrame {
    uint8_t pixels[TumovgmDashboardBufferSize];
} TumovgmDashboardFrame;

typedef struct TumovgmDashboardSnapshot {
    const char* firmware_version;
    const char* git_commit;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    TumovgmDashboardLinkState link_state;
    uint32_t session_id;
    uint32_t uptime_ms;
    uint32_t received_frames;
    uint16_t last_error;
    bool firmware_dirty;
    bool imu_available;
    bool imu_healthy;
} TumovgmDashboardSnapshot;

void tumovgm_dashboard_render(
    TumovgmDashboardFrame* frame,
    const TumovgmDashboardSnapshot* snapshot);

uint8_t tumovgm_dashboard_get_pixel(
    const TumovgmDashboardFrame* frame,
    uint16_t x,
    uint16_t y);

#ifdef __cplusplus
}
#endif

#endif
