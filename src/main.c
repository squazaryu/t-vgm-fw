#include <stdio.h>

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/vreg.h>
#include <hardware/watchdog.h>
#include <pico/stdlib.h>
#include <tumovgm/dashboard.h>
#include <tumovgm/protocol_ids.h>
#include <tumovgm/video_out.h>

#include "bridge_uart.h"

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
    TumovgmFirstGpio = 0,
    TumovgmLastGpio = 29,
    TumovgmHeartbeatPeriodMs = 1000,
    TumovgmDashboardPeriodMs = 250,
    TumovgmLinkFreshnessMs = 5000,
    TumovgmSystemClockKhz = 252000,
    TumovgmWatchdogTimeoutMs = 2000,
};

static TumovgmDashboardFrame tumovgm_dashboard_frame;

static void tumovgm_set_external_gpio_high_impedance(void) {
    for(uint gpio = TumovgmFirstGpio; gpio <= TumovgmLastGpio; gpio++) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static TumovgmDashboardLinkState
    tumovgm_dashboard_link_state(const TumovgmBridgeUartStatus* status, uint32_t now_ms) {
    if(status->last_error == TumovgmErrorUnsupportedVersion &&
       status->peer_major != TUMOVGM_PROTOCOL_MAJOR) {
        return TumovgmDashboardLinkIncompatible;
    }
    if(status->session_active) return TumovgmDashboardLinkActive;
    if(status->negotiated &&
       (uint32_t)(now_ms - status->last_activity_ms) < TumovgmLinkFreshnessMs) {
        return TumovgmDashboardLinkConnected;
    }
    return TumovgmDashboardLinkWaiting;
}

static void tumovgm_update_dashboard(uint32_t now_ms, bool publish) {
    TumovgmBridgeUartStatus status;
    tumovgm_bridge_uart_get_status(&status);
    const TumovgmDashboardSnapshot snapshot = {
        .firmware_version = TUMOVGM_VERSION,
        .git_commit = TUMOVGM_GIT_COMMIT,
        .protocol_major = TUMOVGM_PROTOCOL_MAJOR,
        .protocol_minor = TUMOVGM_PROTOCOL_MINOR,
        .link_state = tumovgm_dashboard_link_state(&status, now_ms),
        .session_id = status.session_active ? status.session_id : 0,
        .uptime_ms = now_ms,
        .received_frames = status.received_frames,
        .last_error = status.last_error,
        .firmware_dirty = TUMOVGM_GIT_DIRTY != 0,
        .imu_available = status.imu_available,
        .imu_healthy = status.imu_healthy,
    };
    tumovgm_dashboard_render(&tumovgm_dashboard_frame, &snapshot);
    if(publish) tumovgm_video_out_publish(&tumovgm_dashboard_frame);
}

int main(void) {
    tumovgm_set_external_gpio_high_impedance();
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(TumovgmSystemClockKhz, true);
    stdio_init_all();
    tumovgm_bridge_uart_init();

    tumovgm_update_dashboard(0, false);
    tumovgm_video_out_init(&tumovgm_dashboard_frame);
    watchdog_enable(TumovgmWatchdogTimeoutMs, true);

    uint32_t last_heartbeat_ms = 0;
    uint32_t last_dashboard_ms = 0;

    while(true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        tumovgm_bridge_uart_poll(now_ms);
        watchdog_update();
        if((uint32_t)(now_ms - last_dashboard_ms) >= TumovgmDashboardPeriodMs) {
            last_dashboard_ms = now_ms;
            tumovgm_update_dashboard(now_ms, true);
        }
        if((uint32_t)(now_ms - last_heartbeat_ms) >= TumovgmHeartbeatPeriodMs) {
            last_heartbeat_ms = now_ms;
            printf(
                "TUMOVGM_BRIDGE version=%s protocol=%d.%d commit=%s dirty=%d "
                "uart=230400 video=640x480@60 frames=%lu watchdog=%dms "
                "gpio=high-z-except-uart+dvi usb_host=off\n",
                TUMOVGM_VERSION,
                TUMOVGM_PROTOCOL_MAJOR,
                TUMOVGM_PROTOCOL_MINOR,
                TUMOVGM_GIT_COMMIT,
                TUMOVGM_GIT_DIRTY,
                (unsigned long)tumovgm_video_out_frame_count(),
                TumovgmWatchdogTimeoutMs);
        }
        tight_loop_contents();
    }
}
