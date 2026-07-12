#include <tumovgm/imu_service.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int16_t tumovgm_clamp_i16(int32_t value) {
    if(value > INT16_MAX) return INT16_MAX;
    if(value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static uint16_t tumovgm_negotiate_rate(uint16_t requested_rate_hz) {
    if(requested_rate_hz >= 50) return 50;
    if(requested_rate_hz >= 25) return 25;
    if(requested_rate_hz >= 10) return 10;
    return 0;
}

static TumovgmImuOrientation tumovgm_orientation(const int16_t acceleration_mg[3]) {
    const int32_t x = acceleration_mg[0];
    const int32_t y = acceleration_mg[1];
    const int32_t z = acceleration_mg[2];
    const int32_t abs_x = abs(x);
    const int32_t abs_y = abs(y);
    const int32_t abs_z = abs(z);
    const int32_t dominant = abs_x > abs_y ? (abs_x > abs_z ? abs_x : abs_z) :
                                             (abs_y > abs_z ? abs_y : abs_z);
    if(dominant < 650) return TumovgmImuOrientationUnknown;
    if(dominant == abs_x) {
        return x >= 0 ? TumovgmImuOrientationXPositive : TumovgmImuOrientationXNegative;
    }
    if(dominant == abs_y) {
        return y >= 0 ? TumovgmImuOrientationYPositive : TumovgmImuOrientationYNegative;
    }
    return z >= 0 ? TumovgmImuOrientationZPositive : TumovgmImuOrientationZNegative;
}

static uint8_t tumovgm_orientation_confidence(const int16_t acceleration_mg[3]) {
    int32_t dominant = abs(acceleration_mg[0]);
    if(abs(acceleration_mg[1]) > dominant) dominant = abs(acceleration_mg[1]);
    if(abs(acceleration_mg[2]) > dominant) dominant = abs(acceleration_mg[2]);
    if(dominant <= 650) return 0;
    if(dominant >= 1100) return 100;
    return (uint8_t)(60 + (dominant - 650) * 40 / 450);
}

static bool tumovgm_is_shake(const TumovgmImuSample* sample) {
    const int32_t acceleration_sum = abs(sample->acceleration_mg[0]) +
                                     abs(sample->acceleration_mg[1]) +
                                     abs(sample->acceleration_mg[2]);
    const int32_t gyro_sum = abs(sample->angular_velocity_deci_dps[0]) +
                             abs(sample->angular_velocity_deci_dps[1]) +
                             abs(sample->angular_velocity_deci_dps[2]);
    return acceleration_sum > 2200 || gyro_sum > 6000;
}

static uint8_t tumovgm_shake_confidence(const TumovgmImuSample* sample) {
    int32_t gyro_sum = abs(sample->angular_velocity_deci_dps[0]) +
                       abs(sample->angular_velocity_deci_dps[1]) +
                       abs(sample->angular_velocity_deci_dps[2]);
    if(gyro_sum >= 12000) return 100;
    if(gyro_sum <= 6000) return 70;
    return (uint8_t)(70 + (gyro_sum - 6000) * 30 / 6000);
}

static void tumovgm_queue_gesture(
    TumovgmImuService* service,
    TumovgmImuGesture gesture,
    uint8_t confidence,
    uint32_t now_ms) {
    if((service->flags & TumovgmImuFlagGestures) == 0 || service->pending_gesture) return;
    if((uint32_t)(now_ms - service->last_gesture_ms) < TumovgmImuGestureDebounceMs) return;
    service->gesture_sequence++;
    if(service->gesture_sequence == 0) service->gesture_sequence++;
    service->latest_gesture = (TumovgmImuGestureEvent){
        .session_id = service->session_id,
        .timestamp_ms = now_ms,
        .sequence = service->gesture_sequence,
        .gesture = gesture,
        .confidence = confidence,
        .orientation = service->orientation,
    };
    service->pending_gesture = true;
    service->last_gesture_ms = now_ms;
}

static void tumovgm_update_orientation(TumovgmImuService* service, uint32_t now_ms) {
    const TumovgmImuOrientation observed = service->latest_sample.orientation;
    if(observed == TumovgmImuOrientationUnknown) {
        service->orientation_candidate = TumovgmImuOrientationUnknown;
        service->orientation_candidate_count = 0;
        return;
    }
    if(observed != service->orientation_candidate) {
        service->orientation_candidate = observed;
        service->orientation_candidate_count = 1;
        return;
    }
    if(service->orientation_candidate_count < UINT8_MAX) service->orientation_candidate_count++;
    if(service->orientation_candidate_count < TumovgmImuStableOrientationSamples ||
       observed == service->orientation) {
        return;
    }
    const bool changed = service->orientation != TumovgmImuOrientationUnknown;
    service->orientation = observed;
    service->latest_sample.orientation = observed;
    if(changed) {
        tumovgm_queue_gesture(
            service,
            TumovgmImuGestureOrientationChanged,
            tumovgm_orientation_confidence(service->latest_sample.acceleration_mg),
            now_ms);
    }
}

static bool tumovgm_take_event(TumovgmImuService* service, TumovgmImuEvent* event) {
    if(service->pending_gesture) {
        *event = (TumovgmImuEvent){
            .kind = TumovgmImuEventGesture,
            .data.gesture = service->latest_gesture,
        };
        service->pending_gesture = false;
        return true;
    }
    if(service->pending_sample && service->credits > 0 &&
       (service->flags & TumovgmImuFlagRawSamples) != 0) {
        *event = (TumovgmImuEvent){
            .kind = TumovgmImuEventSample,
            .data.sample = service->latest_sample,
        };
        service->pending_sample = false;
        service->credits--;
        return true;
    }
    return false;
}

void tumovgm_imu_service_init(
    TumovgmImuService* service,
    const TumovgmImuDriverOps* driver,
    void* driver_context) {
    if(service == NULL || driver == NULL) return;
    memset(service, 0, sizeof(*service));
    service->driver = *driver;
    service->driver_context = driver_context;
    service->health = TumovgmImuHealthUnknown;
    if(service->driver.probe == NULL ||
       !service->driver.probe(driver_context, &service->who_am_i, &service->bus_error)) {
        service->health = TumovgmImuHealthBusError;
    } else if(service->who_am_i != TumovgmImuWhoAmI) {
        service->health = TumovgmImuHealthWrongIdentity;
    } else {
        service->health = TumovgmImuHealthReady;
    }
}

bool tumovgm_imu_service_configure(
    TumovgmImuService* service,
    uint32_t session_id,
    uint16_t requested_rate_hz,
    uint8_t flags,
    uint32_t now_ms,
    uint16_t* actual_rate_hz) {
    if(service == NULL || session_id == 0 || actual_rate_hz == NULL ||
       (flags & (uint8_t)~TumovgmImuKnownFlags) != 0 || flags == 0) {
        return false;
    }
    const uint16_t rate_hz = tumovgm_negotiate_rate(requested_rate_hz);
    if(rate_hz == 0 || service->who_am_i != TumovgmImuWhoAmI || service->driver.start == NULL ||
       !service->driver.start(service->driver_context, &service->bus_error)) {
        service->health = TumovgmImuHealthBusError;
        return false;
    }

    service->session_id = session_id;
    service->rate_hz = rate_hz;
    service->period_ms = 1000U / rate_hz;
    service->next_sample_ms = now_ms;
    service->flags = flags;
    service->credits = 0;
    service->sample_sequence = 0;
    service->gesture_sequence = 0;
    service->last_gesture_ms = now_ms - TumovgmImuGestureDebounceMs;
    memset(service->gyro_bias_sum, 0, sizeof(service->gyro_bias_sum));
    memset(service->gyro_bias, 0, sizeof(service->gyro_bias));
    service->calibration_count = 0;
    service->orientation = TumovgmImuOrientationUnknown;
    service->orientation_candidate = TumovgmImuOrientationUnknown;
    service->orientation_candidate_count = 0;
    service->pending_sample = false;
    service->pending_gesture = false;
    service->running = true;
    service->health = TumovgmImuHealthCalibrating;
    *actual_rate_hz = rate_hz;
    return true;
}

bool tumovgm_imu_service_grant(
    TumovgmImuService* service,
    uint32_t session_id,
    uint16_t credits,
    uint16_t* total_credits) {
    if(service == NULL || !service->running || session_id != service->session_id || credits == 0 ||
       credits > TumovgmImuMaximumCredits ||
       service->credits > (uint16_t)(TumovgmImuMaximumCredits - credits)) {
        return false;
    }
    service->credits = (uint16_t)(service->credits + credits);
    if(total_credits != NULL) *total_credits = service->credits;
    return true;
}

void tumovgm_imu_service_sync_session(
    TumovgmImuService* service,
    bool session_active,
    uint32_t session_id) {
    if(service == NULL || !service->running) return;
    if(!session_active || session_id != service->session_id) tumovgm_imu_service_stop(service);
}

bool tumovgm_imu_service_poll(TumovgmImuService* service, uint32_t now_ms, TumovgmImuEvent* event) {
    if(service == NULL || event == NULL || !service->running) return false;
    *event = (TumovgmImuEvent){0};
    if(tumovgm_take_event(service, event)) return true;
    if((int32_t)(now_ms - service->next_sample_ms) < 0) return false;
    service->next_sample_ms = now_ms + service->period_ms;

    TumovgmImuRawSample raw;
    if(service->driver.read == NULL ||
       !service->driver.read(service->driver_context, &raw, &service->bus_error)) {
        service->health = TumovgmImuHealthBusError;
        tumovgm_imu_service_stop(service);
        return false;
    }

    service->sample_sequence++;
    if(service->sample_sequence == 0) service->sample_sequence++;
    service->latest_sample = (TumovgmImuSample){
        .session_id = service->session_id,
        .timestamp_ms = now_ms,
        .sequence = service->sample_sequence,
        .temperature_centi_c = tumovgm_clamp_i16(2500 + (int32_t)raw.temperature * 10000 / 13248),
        .flags = TumovgmImuSampleFlagHealthy,
    };
    for(uint8_t axis = 0; axis < 3; axis++) {
        service->latest_sample.acceleration_mg[axis] =
            tumovgm_clamp_i16((int32_t)raw.acceleration[axis] * 4000 / 32768);
        if(service->calibration_count < TumovgmImuCalibrationSamples) {
            service->gyro_bias_sum[axis] += raw.angular_velocity[axis];
        }
        const int32_t corrected = (int32_t)raw.angular_velocity[axis] - service->gyro_bias[axis];
        service->latest_sample.angular_velocity_deci_dps[axis] =
            tumovgm_clamp_i16(corrected * 5000 / 32768);
    }

    if(service->calibration_count < TumovgmImuCalibrationSamples) {
        service->calibration_count++;
        if(service->calibration_count == TumovgmImuCalibrationSamples) {
            for(uint8_t axis = 0; axis < 3; axis++) {
                service->gyro_bias[axis] =
                    (int16_t)(service->gyro_bias_sum[axis] / TumovgmImuCalibrationSamples);
            }
            service->health = TumovgmImuHealthReady;
        }
    } else {
        service->latest_sample.flags |= TumovgmImuSampleFlagCalibrated;
    }

    service->latest_sample.orientation =
        tumovgm_orientation(service->latest_sample.acceleration_mg);
    tumovgm_update_orientation(service, now_ms);
    service->latest_sample.orientation = service->orientation;
    if((service->latest_sample.flags & TumovgmImuSampleFlagCalibrated) != 0 &&
       tumovgm_is_shake(&service->latest_sample)) {
        tumovgm_queue_gesture(
            service,
            TumovgmImuGestureShake,
            tumovgm_shake_confidence(&service->latest_sample),
            now_ms);
    }
    service->pending_sample = true;
    return tumovgm_take_event(service, event);
}

void tumovgm_imu_service_stop(TumovgmImuService* service) {
    if(service == NULL) return;
    if(service->running && service->driver.stop != NULL) {
        service->driver.stop(service->driver_context);
    }
    service->running = false;
    service->session_id = 0;
    service->flags = 0;
    service->credits = 0;
    service->pending_sample = false;
    service->pending_gesture = false;
    if(service->health != TumovgmImuHealthBusError &&
       service->health != TumovgmImuHealthWrongIdentity) {
        service->health = TumovgmImuHealthReady;
    }
}
