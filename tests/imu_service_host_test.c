#include <tumovgm/imu_service.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if(!(condition)) {                                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while(false)

typedef struct FakeImu {
    TumovgmImuRawSample sample;
    uint8_t identity;
    uint16_t error;
    uint16_t starts;
    uint16_t stops;
    uint16_t reads;
    bool probe_ok;
    bool start_ok;
    bool read_ok;
} FakeImu;

static bool fake_probe(void* context, uint8_t* identity, uint16_t* error) {
    FakeImu* fake = context;
    *identity = fake->identity;
    *error = fake->error;
    return fake->probe_ok;
}

static bool fake_start(void* context, uint16_t* error) {
    FakeImu* fake = context;
    fake->starts++;
    *error = fake->error;
    return fake->start_ok;
}

static void fake_stop(void* context) {
    FakeImu* fake = context;
    fake->stops++;
}

static bool fake_read(void* context, TumovgmImuRawSample* sample, uint16_t* error) {
    FakeImu* fake = context;
    fake->reads++;
    *sample = fake->sample;
    *error = fake->error;
    return fake->read_ok;
}

static const TumovgmImuDriverOps fake_driver = {
    .probe = fake_probe,
    .start = fake_start,
    .stop = fake_stop,
    .read = fake_read,
};

static FakeImu make_fake(void) {
    FakeImu fake = {
        .identity = TumovgmImuWhoAmI,
        .probe_ok = true,
        .start_ok = true,
        .read_ok = true,
    };
    fake.sample.acceleration[2] = 8192;
    return fake;
}

static bool test_identity_and_rate_negotiation(void) {
    FakeImu fake = make_fake();
    TumovgmImuService service;
    tumovgm_imu_service_init(&service, &fake_driver, &fake);
    CHECK(service.health == TumovgmImuHealthReady);
    CHECK(service.who_am_i == TumovgmImuWhoAmI);

    uint16_t rate = 0;
    CHECK(!tumovgm_imu_service_configure(&service, 1, 9, TumovgmImuFlagRawSamples, 0, &rate));
    CHECK(tumovgm_imu_service_configure(&service, 1, 40, TumovgmImuFlagRawSamples, 0, &rate));
    CHECK(rate == 25);
    CHECK(fake.starts == 1);
    tumovgm_imu_service_stop(&service);
    CHECK(fake.stops == 1);

    fake = make_fake();
    fake.identity = 0x00;
    tumovgm_imu_service_init(&service, &fake_driver, &fake);
    CHECK(service.health == TumovgmImuHealthWrongIdentity);
    CHECK(!tumovgm_imu_service_configure(&service, 1, 50, TumovgmImuFlagRawSamples, 0, &rate));
    return true;
}

static bool test_credit_bounded_stream_and_calibration(void) {
    FakeImu fake = make_fake();
    TumovgmImuService service;
    tumovgm_imu_service_init(&service, &fake_driver, &fake);
    uint16_t rate = 0;
    CHECK(tumovgm_imu_service_configure(&service, 7, 25, TumovgmImuFlagRawSamples, 0, &rate));

    uint16_t total = 0;
    CHECK(!tumovgm_imu_service_grant(&service, 8, 1, &total));
    CHECK(tumovgm_imu_service_grant(&service, 7, 2, &total));
    CHECK(total == 2);
    CHECK(!tumovgm_imu_service_grant(&service, 7, TumovgmImuMaximumCredits, &total));

    TumovgmImuEvent event;
    CHECK(tumovgm_imu_service_poll(&service, 0, &event));
    CHECK(event.kind == TumovgmImuEventSample);
    CHECK(event.data.sample.acceleration_mg[2] == 1000);
    CHECK(tumovgm_imu_service_poll(&service, 40, &event));
    CHECK(!tumovgm_imu_service_poll(&service, 80, &event));
    CHECK(fake.reads == 3);
    CHECK(tumovgm_imu_service_grant(&service, 7, 1, &total));
    CHECK(tumovgm_imu_service_poll(&service, 81, &event));
    CHECK(event.data.sample.sequence == 3);
    CHECK(fake.reads == 3);

    CHECK(tumovgm_imu_service_grant(&service, 7, 29, &total));
    for(uint32_t sample = 3; sample < TumovgmImuCalibrationSamples; sample++) {
        CHECK(tumovgm_imu_service_poll(&service, (sample + 1) * 40, &event));
    }
    CHECK(service.health == TumovgmImuHealthReady);
    CHECK(service.calibration_count == TumovgmImuCalibrationSamples);
    CHECK((event.data.sample.flags & TumovgmImuSampleFlagCalibrated) == 0);

    CHECK(tumovgm_imu_service_grant(&service, 7, 1, &total));
    CHECK(tumovgm_imu_service_poll(&service, 1320, &event));
    CHECK((event.data.sample.flags & TumovgmImuSampleFlagCalibrated) != 0);
    return true;
}

static bool test_orientation_gesture_and_session_release(void) {
    FakeImu fake = make_fake();
    TumovgmImuService service;
    tumovgm_imu_service_init(&service, &fake_driver, &fake);
    uint16_t rate = 0;
    CHECK(tumovgm_imu_service_configure(&service, 11, 10, TumovgmImuFlagGestures, 0, &rate));

    TumovgmImuEvent event;
    CHECK(!tumovgm_imu_service_poll(&service, 0, &event));
    CHECK(!tumovgm_imu_service_poll(&service, 100, &event));
    CHECK(!tumovgm_imu_service_poll(&service, 200, &event));
    CHECK(service.orientation == TumovgmImuOrientationZPositive);

    memset(fake.sample.acceleration, 0, sizeof(fake.sample.acceleration));
    fake.sample.acceleration[1] = 8192;
    CHECK(!tumovgm_imu_service_poll(&service, 800, &event));
    CHECK(!tumovgm_imu_service_poll(&service, 900, &event));
    CHECK(tumovgm_imu_service_poll(&service, 1000, &event));
    CHECK(event.kind == TumovgmImuEventGesture);
    CHECK(event.data.gesture.gesture == TumovgmImuGestureOrientationChanged);
    CHECK(event.data.gesture.orientation == TumovgmImuOrientationYPositive);
    CHECK(event.data.gesture.confidence >= 60);
    CHECK(!tumovgm_imu_service_poll(&service, 1001, &event));

    tumovgm_imu_service_sync_session(&service, false, 0);
    CHECK(!service.running);
    CHECK(fake.stops == 1);
    CHECK(!tumovgm_imu_service_poll(&service, 1200, &event));
    return true;
}

static bool test_read_failure_releases_bus(void) {
    FakeImu fake = make_fake();
    TumovgmImuService service;
    tumovgm_imu_service_init(&service, &fake_driver, &fake);
    uint16_t rate = 0;
    CHECK(tumovgm_imu_service_configure(&service, 4, 50, TumovgmImuFlagRawSamples, 0, &rate));
    fake.read_ok = false;
    fake.error = 42;
    TumovgmImuEvent event;
    CHECK(!tumovgm_imu_service_poll(&service, 0, &event));
    CHECK(service.health == TumovgmImuHealthBusError);
    CHECK(service.bus_error == 42);
    CHECK(fake.stops == 1);
    return true;
}

int main(void) {
    if(!test_identity_and_rate_negotiation() || !test_credit_bounded_stream_and_calibration() ||
       !test_orientation_gesture_and_session_release() || !test_read_failure_releases_bus()) {
        return 1;
    }
    puts("imu_service_host_test: PASS");
    return 0;
}
