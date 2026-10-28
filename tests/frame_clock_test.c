#include "frame_clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
  FrameClock clock;
  frame_clock_reset(&clock, 0);
  for (int i = 0; i < 120; i++) {
    double start = i / 60.0;
    assert(frame_clock_delay(&clock, start) < 1e-8);
    frame_clock_begin(&clock, start);
    assert(fabs(frame_clock_delay(&clock, start + .002) - (1.0/60 - .002)) < 1e-8);
    frame_clock_present(&clock, start + 1.0/60);
  }
  assert(clock.fps > 59 && clock.fps < 61);
  /* A load stall must lower the measured rate, not create a short-dt spike. */
  assert(frame_clock_delay(&clock, 3) == 0);
  frame_clock_begin(&clock, 3);
  float stalled = frame_clock_present(&clock, 3);
  assert(stalled < 30);
  assert(frame_clock_delay(&clock, 3.001) > .015);
  /* Hide/resume starts a new measurement interval with no stale high rate. */
  frame_clock_reset(&clock, 4);
  assert(clock.fps == 0 && frame_clock_delay(&clock, 4) == 0);
  for (int i = 1; i <= 15; i++) frame_clock_present(&clock, 4 + i / 30.0);
  assert(clock.fps > 29 && clock.fps < 31);
  puts("Frame clock: 60 FPS pacing, actual presentation rate, stalls and resume passed");
}
