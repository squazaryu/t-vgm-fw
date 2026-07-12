#include <stdio.h>

#include <hardware/gpio.h>
#include <pico/stdlib.h>

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

    while(true) {
        printf(
            "TUMOVGM_RECOVERY_PROBE version=%s commit=%s dirty=%d "
            "gpio=high-z usb_host=off\n",
            TUMOVGM_VERSION,
            TUMOVGM_GIT_COMMIT,
            TUMOVGM_GIT_DIRTY);
        sleep_ms(TumovgmHeartbeatPeriodMs);
    }
}
