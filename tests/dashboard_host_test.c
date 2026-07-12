#include <tumovgm/dashboard.h>

#include <stdbool.h>
#include <stdio.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if(!(condition)) {                                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while(false)

static uint32_t frame_hash(const TumovgmDashboardFrame* frame) {
    uint32_t hash = UINT32_C(2166136261);
    for(size_t index = 0; index < sizeof(frame->pixels); index++) {
        hash ^= frame->pixels[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static size_t color_count(const TumovgmDashboardFrame* frame, uint8_t color) {
    size_t count = 0;
    for(uint16_t y = 0; y < TumovgmDashboardHeight; y++) {
        for(uint16_t x = 0; x < TumovgmDashboardWidth; x++) {
            if(tumovgm_dashboard_get_pixel(frame, x, y) == color) count++;
        }
    }
    return count;
}

static TumovgmDashboardSnapshot snapshot(TumovgmDashboardLinkState state) {
    return (TumovgmDashboardSnapshot){
        .firmware_version = "t-vgm-dev-001-005",
        .git_commit = "0123456789ab",
        .protocol_major = 1,
        .protocol_minor = 1,
        .link_state = state,
        .session_id = state == TumovgmDashboardLinkActive ? 42 : 0,
        .uptime_ms = 123456,
        .received_frames = 17,
        .last_error = state == TumovgmDashboardLinkIncompatible ? 1 : 0,
        .firmware_dirty = false,
        .imu_available = false,
        .imu_healthy = false,
    };
}

static bool test_dashboard_states(void) {
    TumovgmDashboardFrame waiting;
    TumovgmDashboardSnapshot state = snapshot(TumovgmDashboardLinkWaiting);
    tumovgm_dashboard_render(&waiting, &state);
    CHECK(color_count(&waiting, TumovgmDashboardColorOrange) > 8000);
    CHECK(color_count(&waiting, TumovgmDashboardColorText) > 500);
    CHECK(color_count(&waiting, TumovgmDashboardColorYellow) > 100);
    CHECK(tumovgm_dashboard_get_pixel(&waiting, 319, 239) == TumovgmDashboardColorBackground);
    CHECK(frame_hash(&waiting) == UINT32_C(0x71B7F38F));

    TumovgmDashboardFrame active;
    state = snapshot(TumovgmDashboardLinkActive);
    tumovgm_dashboard_render(&active, &state);
    CHECK(color_count(&active, TumovgmDashboardColorGreen) > 100);
    CHECK(frame_hash(&active) != frame_hash(&waiting));
    CHECK(frame_hash(&active) == UINT32_C(0xDB3063A0));

    TumovgmDashboardFrame incompatible;
    state = snapshot(TumovgmDashboardLinkIncompatible);
    tumovgm_dashboard_render(&incompatible, &state);
    CHECK(color_count(&incompatible, TumovgmDashboardColorRed) > 200);
    CHECK(frame_hash(&incompatible) != frame_hash(&active));
    CHECK(frame_hash(&incompatible) == UINT32_C(0x3F785A5F));

    TumovgmDashboardFrame imu_ready;
    state = snapshot(TumovgmDashboardLinkWaiting);
    state.imu_available = true;
    state.imu_healthy = true;
    tumovgm_dashboard_render(&imu_ready, &state);
    CHECK(frame_hash(&imu_ready) != frame_hash(&waiting));
    CHECK(color_count(&imu_ready, TumovgmDashboardColorRed) == 0);

    printf(
        "dashboard hashes: waiting=%08lx active=%08lx incompatible=%08lx\n",
        (unsigned long)frame_hash(&waiting),
        (unsigned long)frame_hash(&active),
        (unsigned long)frame_hash(&incompatible));
    return true;
}

static bool write_preview(const char* path) {
    static const uint8_t palette[16][3] = {
        [TumovgmDashboardColorBackground] = {8, 12, 16},
        [TumovgmDashboardColorSurface] = {24, 28, 28},
        [TumovgmDashboardColorOrange] = {255, 126, 0},
        [TumovgmDashboardColorText] = {255, 255, 255},
        [TumovgmDashboardColorMuted] = {156, 158, 156},
        [TumovgmDashboardColorGreen] = {72, 206, 72},
        [TumovgmDashboardColorRed] = {244, 82, 48},
        [TumovgmDashboardColorYellow] = {255, 216, 0},
        [TumovgmDashboardColorBlack] = {0, 0, 0},
    };
    FILE* output = fopen(path, "wb");
    if(output == NULL) return false;
    fprintf(output, "P6\n%d %d\n255\n", TumovgmDashboardWidth, TumovgmDashboardHeight);

    TumovgmDashboardFrame frame;
    TumovgmDashboardSnapshot state = snapshot(TumovgmDashboardLinkActive);
    tumovgm_dashboard_render(&frame, &state);
    for(uint16_t y = 0; y < TumovgmDashboardHeight; y++) {
        for(uint16_t x = 0; x < TumovgmDashboardWidth; x++) {
            const uint8_t color = tumovgm_dashboard_get_pixel(&frame, x, y);
            if(fwrite(palette[color], 1, 3, output) != 3) {
                fclose(output);
                return false;
            }
        }
    }
    return fclose(output) == 0;
}

int main(int argc, char** argv) {
    if(!test_dashboard_states()) return 1;
    if(argc == 2 && !write_preview(argv[1])) return 1;
    puts("dashboard_host_test: PASS");
    return 0;
}
