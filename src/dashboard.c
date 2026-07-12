#include <tumovgm/dashboard.h>

#include <stdio.h>
#include <string.h>

typedef struct TumovgmGlyph {
    char character;
    uint8_t rows[7];
} TumovgmGlyph;

static const TumovgmGlyph tumovgm_glyphs[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'#', {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

static const uint8_t tumovgm_blank_glyph[7] = {0};

static char tumovgm_uppercase(char character) {
    if(character >= 'a' && character <= 'z') return (char)(character - ('a' - 'A'));
    return character;
}

static const uint8_t* tumovgm_find_glyph(char character) {
    if(character == ' ') return tumovgm_blank_glyph;
    character = tumovgm_uppercase(character);
    for(size_t index = 0; index < sizeof(tumovgm_glyphs) / sizeof(tumovgm_glyphs[0]);
        index++) {
        if(tumovgm_glyphs[index].character == character) return tumovgm_glyphs[index].rows;
    }
    return tumovgm_find_glyph('?');
}

static void tumovgm_set_pixel(
    TumovgmDashboardFrame* frame,
    uint16_t x,
    uint16_t y,
    TumovgmDashboardColor color) {
    if(x >= TumovgmDashboardWidth || y >= TumovgmDashboardHeight) return;
    const uint32_t pixel = (uint32_t)y * TumovgmDashboardWidth + x;
    uint8_t* packed = &frame->pixels[pixel / TumovgmDashboardPixelsPerByte];
    if((pixel & 1U) == 0) {
        *packed = (uint8_t)((*packed & 0x0F) | ((uint8_t)color << 4));
    } else {
        *packed = (uint8_t)((*packed & 0xF0) | (uint8_t)color);
    }
}

uint8_t tumovgm_dashboard_get_pixel(
    const TumovgmDashboardFrame* frame,
    uint16_t x,
    uint16_t y) {
    if(frame == NULL || x >= TumovgmDashboardWidth || y >= TumovgmDashboardHeight) {
        return TumovgmDashboardColorBackground;
    }
    const uint32_t pixel = (uint32_t)y * TumovgmDashboardWidth + x;
    const uint8_t packed = frame->pixels[pixel / TumovgmDashboardPixelsPerByte];
    return (pixel & 1U) == 0 ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0F);
}

static void tumovgm_fill_rect(
    TumovgmDashboardFrame* frame,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    TumovgmDashboardColor color) {
    const uint16_t x_end = x + width > TumovgmDashboardWidth ? TumovgmDashboardWidth : x + width;
    const uint16_t y_end = y + height > TumovgmDashboardHeight ? TumovgmDashboardHeight : y + height;
    for(uint16_t row = y; row < y_end; row++) {
        for(uint16_t column = x; column < x_end; column++) {
            tumovgm_set_pixel(frame, column, row, color);
        }
    }
}

static uint16_t tumovgm_text_width(const char* text, uint8_t scale) {
    const size_t length = text == NULL ? 0 : strlen(text);
    if(length == 0) return 0;
    return (uint16_t)(length * 6U * scale - scale);
}

static void tumovgm_draw_text(
    TumovgmDashboardFrame* frame,
    uint16_t x,
    uint16_t y,
    const char* text,
    uint8_t scale,
    TumovgmDashboardColor color) {
    if(text == NULL || scale == 0) return;
    for(size_t character_index = 0; text[character_index] != '\0'; character_index++) {
        const uint8_t* rows = tumovgm_find_glyph(text[character_index]);
        for(uint8_t row = 0; row < 7; row++) {
            for(uint8_t column = 0; column < 5; column++) {
                if((rows[row] & (0x10U >> column)) == 0) continue;
                tumovgm_fill_rect(
                    frame,
                    (uint16_t)(x + column * scale),
                    (uint16_t)(y + row * scale),
                    scale,
                    scale,
                    color);
            }
        }
        x = (uint16_t)(x + 6U * scale);
        if(x >= TumovgmDashboardWidth) return;
    }
}

static void tumovgm_draw_text_right(
    TumovgmDashboardFrame* frame,
    uint16_t right,
    uint16_t y,
    const char* text,
    uint8_t scale,
    TumovgmDashboardColor color) {
    const uint16_t width = tumovgm_text_width(text, scale);
    tumovgm_draw_text(frame, right > width ? right - width : 0, y, text, scale, color);
}

static const char* tumovgm_link_text(TumovgmDashboardLinkState state) {
    switch(state) {
    case TumovgmDashboardLinkConnected:
        return "FLIPPER CONNECTED";
    case TumovgmDashboardLinkActive:
        return "SESSION ACTIVE";
    case TumovgmDashboardLinkIncompatible:
        return "INCOMPATIBLE CLIENT";
    case TumovgmDashboardLinkError:
        return "LINK ERROR";
    case TumovgmDashboardLinkWaiting:
    default:
        return "WAITING FOR FLIPPER";
    }
}

static TumovgmDashboardColor tumovgm_link_color(TumovgmDashboardLinkState state) {
    switch(state) {
    case TumovgmDashboardLinkConnected:
    case TumovgmDashboardLinkActive:
        return TumovgmDashboardColorGreen;
    case TumovgmDashboardLinkIncompatible:
    case TumovgmDashboardLinkError:
        return TumovgmDashboardColorRed;
    case TumovgmDashboardLinkWaiting:
    default:
        return TumovgmDashboardColorYellow;
    }
}

static const char* tumovgm_error_text(uint16_t error) {
    switch(error) {
    case 0:
        return "NONE";
    case 1:
        return "UNSUPPORTED VERSION";
    case 2:
        return "UNSUPPORTED MESSAGE";
    case 3:
        return "MALFORMED FRAME";
    case 4:
        return "BAD STATE";
    case 5:
        return "BUSY";
    case 6:
        return "TIMEOUT";
    case 7:
        return "CANCELLED";
    case 8:
        return "NO CAPABILITY";
    case 9:
        return "OVERFLOW";
    case 10:
        return "INTERNAL";
    default:
        return "UNKNOWN";
    }
}

static void tumovgm_draw_row(
    TumovgmDashboardFrame* frame,
    uint16_t y,
    const char* label,
    const char* value,
    TumovgmDashboardColor value_color) {
    tumovgm_draw_text(frame, 12, y, label, 1, TumovgmDashboardColorMuted);
    tumovgm_draw_text(frame, 106, y, value, 1, value_color);
    tumovgm_fill_rect(frame, 12, (uint16_t)(y + 13), 296, 1, TumovgmDashboardColorSurface);
}

void tumovgm_dashboard_render(
    TumovgmDashboardFrame* frame,
    const TumovgmDashboardSnapshot* snapshot) {
    if(frame == NULL || snapshot == NULL) return;

    memset(frame->pixels, 0, sizeof(frame->pixels));
    tumovgm_fill_rect(
        frame, 0, 0, TumovgmDashboardWidth, 32, TumovgmDashboardColorOrange);
    tumovgm_draw_text(frame, 12, 8, "TUMOVGM", 2, TumovgmDashboardColorBlack);
    tumovgm_draw_text_right(
        frame, 308, 12, "001-005", 1, TumovgmDashboardColorBlack);
    if(snapshot->firmware_dirty) {
        tumovgm_draw_text_right(frame, 252, 12, "DIRTY", 1, TumovgmDashboardColorRed);
    }

    const TumovgmDashboardColor link_color = tumovgm_link_color(snapshot->link_state);
    tumovgm_fill_rect(frame, 12, 44, 8, 28, link_color);
    tumovgm_draw_text(
        frame, 32, 44, tumovgm_link_text(snapshot->link_state), 2, TumovgmDashboardColorText);

    char line[40];
    snprintf(line, sizeof(line), "UP %lus", (unsigned long)(snapshot->uptime_ms / 1000U));
    tumovgm_draw_text_right(frame, 308, 67, line, 1, TumovgmDashboardColorMuted);
    tumovgm_fill_rect(frame, 12, 80, 296, 2, TumovgmDashboardColorOrange);

    snprintf(
        line,
        sizeof(line),
        "%u.%u / VIDEO OUT",
        snapshot->protocol_major,
        snapshot->protocol_minor);
    tumovgm_draw_row(frame, 90, "PROTOCOL", line, TumovgmDashboardColorText);

    tumovgm_draw_row(
        frame,
        114,
        "FIRMWARE",
        snapshot->firmware_version != NULL ? snapshot->firmware_version : "UNKNOWN",
        TumovgmDashboardColorText);

    if(snapshot->session_id != 0) {
        snprintf(line, sizeof(line), "ACTIVE #%lu", (unsigned long)snapshot->session_id);
    } else {
        snprintf(line, sizeof(line), "IDLE / RX %lu", (unsigned long)snapshot->received_frames);
    }
    tumovgm_draw_row(frame, 138, "SESSION", line, link_color);

    const char* imu_state = snapshot->imu_available ?
                                (snapshot->imu_healthy ? "READY" : "OFFLINE") :
                                "NOT ENABLED";
    tumovgm_draw_row(
        frame,
        162,
        "IMU",
        imu_state,
        snapshot->imu_available && !snapshot->imu_healthy ? TumovgmDashboardColorRed :
                                                            TumovgmDashboardColorText);

    tumovgm_draw_row(
        frame,
        186,
        "ERROR",
        tumovgm_error_text(snapshot->last_error),
        snapshot->last_error == 0 ? TumovgmDashboardColorGreen : TumovgmDashboardColorRed);

    char commit[20];
    snprintf(
        commit,
        sizeof(commit),
        "COMMIT %.12s",
        snapshot->git_commit != NULL ? snapshot->git_commit : "UNKNOWN");
    tumovgm_draw_text(frame, 12, 222, commit, 1, TumovgmDashboardColorMuted);
    tumovgm_draw_text(frame, 144, 222, "DIAGNOSTICS", 1, TumovgmDashboardColorMuted);
    tumovgm_draw_text_right(
        frame, 308, 222, "NO RAW DATA", 1, TumovgmDashboardColorMuted);
}
