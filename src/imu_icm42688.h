#ifndef TUMOVGM_IMU_ICM42688_H
#define TUMOVGM_IMU_ICM42688_H

#include <stdbool.h>

#include <tumovgm/imu_service.h>

typedef struct TumovgmIcm42688 {
    bool bus_active;
} TumovgmIcm42688;

void tumovgm_icm42688_init(TumovgmIcm42688* imu);
const TumovgmImuDriverOps* tumovgm_icm42688_driver(void);

#endif
