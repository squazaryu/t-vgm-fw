#include "imu_icm42688.h"

#include <string.h>

#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/stdlib.h>

enum {
    TumovgmImuSpiBaud = 1000 * 1000,
    TumovgmImuPinSck = 2,
    TumovgmImuPinMosi = 3,
    TumovgmImuPinMiso = 4,
    TumovgmImuPinCs = 5,
    TumovgmIcmDeviceConfig = 0x11,
    TumovgmIcmTempData1 = 0x1D,
    TumovgmIcmIntfConfig0 = 0x4C,
    TumovgmIcmPowerMgmt0 = 0x4E,
    TumovgmIcmGyroConfig0 = 0x4F,
    TumovgmIcmAccelConfig0 = 0x50,
    TumovgmIcmWhoAmIRegister = 0x75,
    TumovgmIcmAccel4G100Hz = 0x48,
    TumovgmIcmGyro500Dps100Hz = 0x48,
    TumovgmIcmSensorsLowNoise = 0x0F,
    TumovgmIcmErrorWrite = 1,
    TumovgmIcmErrorRead = 2,
    TumovgmIcmErrorIdentity = 3,
};

static void tumovgm_imu_pin_high_impedance(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
}

static void tumovgm_imu_bus_start(TumovgmIcm42688* imu) {
    gpio_init(TumovgmImuPinCs);
    gpio_set_dir(TumovgmImuPinCs, GPIO_OUT);
    gpio_put(TumovgmImuPinCs, 1);
    spi_init(spi0, TumovgmImuSpiBaud);
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(TumovgmImuPinSck, GPIO_FUNC_SPI);
    gpio_set_function(TumovgmImuPinMosi, GPIO_FUNC_SPI);
    gpio_set_function(TumovgmImuPinMiso, GPIO_FUNC_SPI);
    imu->bus_active = true;
}

static void tumovgm_imu_bus_stop(TumovgmIcm42688* imu) {
    if(!imu->bus_active) return;
    spi_deinit(spi0);
    tumovgm_imu_pin_high_impedance(TumovgmImuPinSck);
    tumovgm_imu_pin_high_impedance(TumovgmImuPinMosi);
    tumovgm_imu_pin_high_impedance(TumovgmImuPinMiso);
    tumovgm_imu_pin_high_impedance(TumovgmImuPinCs);
    imu->bus_active = false;
}

static bool tumovgm_imu_write(uint8_t address, uint8_t value) {
    const uint8_t command[2] = {(uint8_t)(address & 0x7F), value};
    gpio_put(TumovgmImuPinCs, 0);
    const bool success = spi_write_blocking(spi0, command, sizeof(command)) ==
                         (int)sizeof(command);
    gpio_put(TumovgmImuPinCs, 1);
    return success;
}

static bool tumovgm_imu_read(uint8_t address, uint8_t* data, size_t size) {
    const uint8_t command = (uint8_t)(address | 0x80);
    gpio_put(TumovgmImuPinCs, 0);
    const bool success = spi_write_blocking(spi0, &command, 1) == 1 &&
                         spi_read_blocking(spi0, 0, data, size) == (int)size;
    gpio_put(TumovgmImuPinCs, 1);
    return success;
}

static int16_t tumovgm_read_i16_le(const uint8_t* data) {
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool
    tumovgm_imu_reset_and_identify(TumovgmIcm42688* imu, uint8_t* who_am_i, uint16_t* bus_error) {
    tumovgm_imu_bus_start(imu);
    if(!tumovgm_imu_write(TumovgmIcmDeviceConfig, 0x01)) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorWrite;
        return false;
    }
    sleep_ms(10);
    if(!tumovgm_imu_read(TumovgmIcmWhoAmIRegister, who_am_i, 1)) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorRead;
        return false;
    }
    if(*who_am_i != TumovgmImuWhoAmI) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorIdentity;
        return false;
    }
    if(bus_error != NULL) *bus_error = 0;
    return true;
}

static bool tumovgm_imu_driver_probe(void* context, uint8_t* who_am_i, uint16_t* bus_error) {
    TumovgmIcm42688* imu = context;
    const bool success = tumovgm_imu_reset_and_identify(imu, who_am_i, bus_error);
    if(success) tumovgm_imu_write(TumovgmIcmPowerMgmt0, 0);
    tumovgm_imu_bus_stop(imu);
    return success;
}

static bool tumovgm_imu_driver_start(void* context, uint16_t* bus_error) {
    TumovgmIcm42688* imu = context;
    uint8_t who_am_i = 0;
    if(!tumovgm_imu_reset_and_identify(imu, &who_am_i, bus_error)) {
        tumovgm_imu_bus_stop(imu);
        return false;
    }
    const bool configured = tumovgm_imu_write(TumovgmIcmIntfConfig0, 0) &&
                            tumovgm_imu_write(TumovgmIcmPowerMgmt0, TumovgmIcmSensorsLowNoise);
    if(!configured) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorWrite;
        tumovgm_imu_bus_stop(imu);
        return false;
    }
    sleep_ms(50);
    if(!tumovgm_imu_write(TumovgmIcmAccelConfig0, TumovgmIcmAccel4G100Hz) ||
       !tumovgm_imu_write(TumovgmIcmGyroConfig0, TumovgmIcmGyro500Dps100Hz)) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorWrite;
        tumovgm_imu_bus_stop(imu);
        return false;
    }
    if(bus_error != NULL) *bus_error = 0;
    return true;
}

static void tumovgm_imu_driver_stop(void* context) {
    TumovgmIcm42688* imu = context;
    if(imu->bus_active) tumovgm_imu_write(TumovgmIcmPowerMgmt0, 0);
    tumovgm_imu_bus_stop(imu);
}

static bool
    tumovgm_imu_driver_read(void* context, TumovgmImuRawSample* sample, uint16_t* bus_error) {
    TumovgmIcm42688* imu = context;
    if(!imu->bus_active || sample == NULL) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorRead;
        return false;
    }
    uint8_t data[14];
    if(!tumovgm_imu_read(TumovgmIcmTempData1, data, sizeof(data))) {
        if(bus_error != NULL) *bus_error = TumovgmIcmErrorRead;
        return false;
    }
    sample->temperature = tumovgm_read_i16_le(data);
    for(uint8_t axis = 0; axis < 3; axis++) {
        sample->acceleration[axis] = tumovgm_read_i16_le(data + 2 + axis * 2);
        sample->angular_velocity[axis] = tumovgm_read_i16_le(data + 8 + axis * 2);
    }
    if(bus_error != NULL) *bus_error = 0;
    return true;
}

void tumovgm_icm42688_init(TumovgmIcm42688* imu) {
    if(imu == NULL) return;
    memset(imu, 0, sizeof(*imu));
}

const TumovgmImuDriverOps* tumovgm_icm42688_driver(void) {
    static const TumovgmImuDriverOps driver = {
        .probe = tumovgm_imu_driver_probe,
        .start = tumovgm_imu_driver_start,
        .stop = tumovgm_imu_driver_stop,
        .read = tumovgm_imu_driver_read,
    };
    return &driver;
}
