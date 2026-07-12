#ifndef TUMOVGM_IMU_SERVICE_H
#define TUMOVGM_IMU_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TumovgmImuWhoAmI = 0x47,
    TumovgmImuStreamId = 1,
    TumovgmImuMinimumRateHz = 10,
    TumovgmImuMaximumRateHz = 50,
    TumovgmImuMaximumCredits = 32,
    TumovgmImuCalibrationSamples = 32,
    TumovgmImuGestureDebounceMs = 800,
    TumovgmImuStableOrientationSamples = 6,
    TumovgmImuOrientationEnterThresholdMg = 800,
    TumovgmImuOrientationHoldThresholdMg = 550,
    TumovgmImuOrientationSwitchMarginMg = 180,
    TumovgmImuFlagRawSamples = 1U << 0,
    TumovgmImuFlagGestures = 1U << 1,
    TumovgmImuKnownFlags = TumovgmImuFlagRawSamples | TumovgmImuFlagGestures,
    TumovgmImuSampleFlagHealthy = 1U << 0,
    TumovgmImuSampleFlagCalibrated = 1U << 1,
};

typedef enum TumovgmImuHealth {
    TumovgmImuHealthUnknown = 0,
    TumovgmImuHealthReady = 1,
    TumovgmImuHealthBusError = 2,
    TumovgmImuHealthWrongIdentity = 3,
    TumovgmImuHealthCalibrating = 4,
} TumovgmImuHealth;

typedef enum TumovgmImuOrientation {
    TumovgmImuOrientationUnknown = 0,
    TumovgmImuOrientationXPositive = 1,
    TumovgmImuOrientationXNegative = 2,
    TumovgmImuOrientationYPositive = 3,
    TumovgmImuOrientationYNegative = 4,
    TumovgmImuOrientationZPositive = 5,
    TumovgmImuOrientationZNegative = 6,
} TumovgmImuOrientation;

typedef enum TumovgmImuGesture {
    TumovgmImuGestureOrientationChanged = 1,
    TumovgmImuGestureShake = 2,
} TumovgmImuGesture;

typedef struct TumovgmImuRawSample {
    int16_t temperature;
    int16_t acceleration[3];
    int16_t angular_velocity[3];
} TumovgmImuRawSample;

typedef struct TumovgmImuDriverOps {
    bool (*probe)(void* context, uint8_t* who_am_i, uint16_t* bus_error);
    bool (*start)(void* context, uint16_t* bus_error);
    void (*stop)(void* context);
    bool (*read)(void* context, TumovgmImuRawSample* sample, uint16_t* bus_error);
} TumovgmImuDriverOps;

typedef struct TumovgmImuSample {
    uint32_t session_id;
    uint32_t timestamp_ms;
    uint16_t sequence;
    int16_t temperature_centi_c;
    int16_t acceleration_mg[3];
    int16_t angular_velocity_deci_dps[3];
    TumovgmImuOrientation orientation;
    uint8_t flags;
} TumovgmImuSample;

typedef struct TumovgmImuGestureEvent {
    uint32_t session_id;
    uint32_t timestamp_ms;
    uint16_t sequence;
    TumovgmImuGesture gesture;
    uint8_t confidence;
    TumovgmImuOrientation orientation;
} TumovgmImuGestureEvent;

typedef enum TumovgmImuEventKind {
    TumovgmImuEventNone = 0,
    TumovgmImuEventSample,
    TumovgmImuEventGesture,
} TumovgmImuEventKind;

typedef struct TumovgmImuEvent {
    TumovgmImuEventKind kind;
    union {
        TumovgmImuSample sample;
        TumovgmImuGestureEvent gesture;
    } data;
} TumovgmImuEvent;

typedef struct TumovgmImuService {
    TumovgmImuDriverOps driver;
    void* driver_context;
    TumovgmImuHealth health;
    uint8_t who_am_i;
    uint8_t flags;
    uint16_t bus_error;
    uint16_t rate_hz;
    uint16_t credits;
    uint16_t sample_sequence;
    uint16_t gesture_sequence;
    uint32_t session_id;
    uint32_t period_ms;
    uint32_t next_sample_ms;
    uint32_t last_gesture_ms;
    int32_t gyro_bias_sum[3];
    int16_t gyro_bias[3];
    uint8_t calibration_count;
    TumovgmImuOrientation orientation;
    TumovgmImuOrientation orientation_candidate;
    uint8_t orientation_candidate_count;
    bool running;
    bool pending_sample;
    bool pending_gesture;
    TumovgmImuSample latest_sample;
    TumovgmImuGestureEvent latest_gesture;
} TumovgmImuService;

void tumovgm_imu_service_init(
    TumovgmImuService* service,
    const TumovgmImuDriverOps* driver,
    void* driver_context);

bool tumovgm_imu_service_configure(
    TumovgmImuService* service,
    uint32_t session_id,
    uint16_t requested_rate_hz,
    uint8_t flags,
    uint32_t now_ms,
    uint16_t* actual_rate_hz);

bool tumovgm_imu_service_grant(
    TumovgmImuService* service,
    uint32_t session_id,
    uint16_t credits,
    uint16_t* total_credits);

void tumovgm_imu_service_sync_session(
    TumovgmImuService* service,
    bool session_active,
    uint32_t session_id);

bool tumovgm_imu_service_poll(TumovgmImuService* service, uint32_t now_ms, TumovgmImuEvent* event);

void tumovgm_imu_service_stop(TumovgmImuService* service);

#ifdef __cplusplus
}
#endif

#endif
