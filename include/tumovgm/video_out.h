#ifndef TUMOVGM_VIDEO_OUT_H
#define TUMOVGM_VIDEO_OUT_H

#include <stdint.h>

#include <tumovgm/dashboard.h>

#ifdef __cplusplus
extern "C" {
#endif

void tumovgm_video_out_init(const TumovgmDashboardFrame* initial_frame);
void tumovgm_video_out_publish(const TumovgmDashboardFrame* frame);
uint32_t tumovgm_video_out_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif
