#include <tumovgm/video_out.h>

#include <string.h>

#include <common_dvi_pin_configs.h>
#include <dvi.h>
#include <dvi_serialiser.h>
#include <hardware/structs/bus_ctrl.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/util/queue.h>

enum {
    TumovgmVideoLogicalWidth = 320,
    TumovgmVideoLogicalHeight = 240,
};

static struct dvi_inst tumovgm_video_dvi;
static TumovgmDashboardFrame tumovgm_video_frames[2];
static uint16_t tumovgm_video_scanlines[2][TumovgmVideoLogicalWidth];
static volatile uint8_t tumovgm_video_active_frame;
static volatile uint8_t tumovgm_video_published_frame;
static volatile uint32_t tumovgm_video_frames_displayed;

static uint16_t tumovgm_video_palette[16] = {
    [TumovgmDashboardColorBackground] = 0x0861,
    [TumovgmDashboardColorSurface] = 0x18E3,
    [TumovgmDashboardColorOrange] = 0xFBE0,
    [TumovgmDashboardColorText] = 0xFFFF,
    [TumovgmDashboardColorMuted] = 0x9CF3,
    [TumovgmDashboardColorGreen] = 0x4E69,
    [TumovgmDashboardColorRed] = 0xF2A6,
    [TumovgmDashboardColorYellow] = 0xFEC0,
    [TumovgmDashboardColorBlack] = 0x0000,
};

static void __not_in_flash_func(tumovgm_video_fill_scanline)(uint16_t* buffer, uint16_t y) {
    if(y == 0) {
        tumovgm_video_active_frame = tumovgm_video_published_frame;
        __dmb();
        tumovgm_video_frames_displayed++;
    }

    const TumovgmDashboardFrame* frame = &tumovgm_video_frames[tumovgm_video_active_frame];
    const uint32_t row_offset = (uint32_t)y * TumovgmVideoLogicalWidth;
    for(uint16_t x = 0; x < TumovgmVideoLogicalWidth; x++) {
        const uint32_t pixel = row_offset + x;
        const uint8_t packed = frame->pixels[pixel / TumovgmDashboardPixelsPerByte];
        const uint8_t color =
            (pixel & 1U) == 0 ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0F);
        buffer[x] = tumovgm_video_palette[color & 0x0F];
    }
}

static void __not_in_flash_func(tumovgm_video_scanline_callback)(void) {
    static uint16_t scanline = 2;
    uint16_t* buffer = NULL;
    if(!queue_try_remove_u32(&tumovgm_video_dvi.q_colour_free, &buffer)) return;

    tumovgm_video_fill_scanline(buffer, scanline);
    queue_add_blocking_u32(&tumovgm_video_dvi.q_colour_valid, &buffer);
    scanline = (uint16_t)((scanline + 1U) % TumovgmVideoLogicalHeight);
}

static void __not_in_flash_func(tumovgm_video_core1_main)(void) {
    dvi_register_irqs_this_core(&tumovgm_video_dvi, DMA_IRQ_0);
    dvi_start(&tumovgm_video_dvi);
    dvi_scanbuf_main_16bpp(&tumovgm_video_dvi);
    __builtin_unreachable();
}

void tumovgm_video_out_init(const TumovgmDashboardFrame* initial_frame) {
    memset(tumovgm_video_frames, 0, sizeof(tumovgm_video_frames));
    if(initial_frame != NULL) {
        memcpy(&tumovgm_video_frames[0], initial_frame, sizeof(*initial_frame));
        memcpy(&tumovgm_video_frames[1], initial_frame, sizeof(*initial_frame));
    }
    tumovgm_video_active_frame = 0;
    tumovgm_video_published_frame = 0;
    tumovgm_video_frames_displayed = 0;

    tumovgm_video_dvi.timing = &dvi_timing_640x480p_60hz;
    tumovgm_video_dvi.ser_cfg = picodvi_dvi_cfg;
    tumovgm_video_dvi.scanline_callback = tumovgm_video_scanline_callback;
    dvi_init(
        &tumovgm_video_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    tumovgm_video_fill_scanline(tumovgm_video_scanlines[0], 0);
    tumovgm_video_fill_scanline(tumovgm_video_scanlines[1], 1);
    uint16_t* buffer = tumovgm_video_scanlines[0];
    queue_add_blocking_u32(&tumovgm_video_dvi.q_colour_valid, &buffer);
    buffer = tumovgm_video_scanlines[1];
    queue_add_blocking_u32(&tumovgm_video_dvi.q_colour_valid, &buffer);

    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(tumovgm_video_core1_main);
}

void tumovgm_video_out_publish(const TumovgmDashboardFrame* frame) {
    if(frame == NULL) return;
    if(tumovgm_video_published_frame != tumovgm_video_active_frame) return;
    const uint8_t target = (uint8_t)(1U - tumovgm_video_active_frame);
    memcpy(&tumovgm_video_frames[target], frame, sizeof(*frame));
    __dmb();
    tumovgm_video_published_frame = target;
}

uint32_t tumovgm_video_out_frame_count(void) {
    return tumovgm_video_frames_displayed;
}
