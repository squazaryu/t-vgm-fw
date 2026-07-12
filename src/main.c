#include <stdio.h>

#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <tumovgm/protocol_ids.h>

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
};

static void tumovgm_set_external_gpio_high_impedance(void) {
    for(uint gpio = TumovgmFirstGpio; gpio <= TumovgmLastGpio; gpio++) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

int main(void) {
    tumovgm_set_external_gpio_high_impedance();
    stdio_init_all();
    tumovgm_bridge_uart_init();

    uint32_t last_heartbeat_ms = 0;

    while(true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        tumovgm_bridge_uart_poll(now_ms);
        if((uint32_t)(now_ms - last_heartbeat_ms) >= TumovgmHeartbeatPeriodMs) {
            last_heartbeat_ms = now_ms;
            printf(
                "TUMOVGM_BRIDGE version=%s protocol=%d.%d commit=%s dirty=%d "
                "uart=230400 gpio=high-z-except-uart usb_host=off\n",
                TUMOVGM_VERSION,
                TUMOVGM_PROTOCOL_MAJOR,
                TUMOVGM_PROTOCOL_MINOR,
                TUMOVGM_GIT_COMMIT,
                TUMOVGM_GIT_DIRTY);
        }
        tight_loop_contents();
    }
}
