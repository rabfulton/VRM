#ifndef HIT_CAPTURE_H
#define HIT_CAPTURE_H
#include "input_mask.h"
#include "platform.h"
#include <epoxy/gl.h>

typedef struct {
  PlatformWindow *window;
  InputMask mask;
  GLuint fbo, depth_stencil, buffers[2];
  GLsync fences[2];
  int width, height, window_w, window_h;
  unsigned next_slot;
  double next_capture;
} HitCapture;

void hit_capture_init(HitCapture *capture, PlatformWindow *window);
void hit_capture_reset(HitCapture *capture);
void hit_capture_resize(HitCapture *capture, int window_width, int window_height);
void hit_capture_cleanup(HitCapture *capture);
void hit_capture_poll(HitCapture *capture);
void hit_capture_submit(HitCapture *capture, GLuint source_fbo, int width, int height, double now);
#endif
