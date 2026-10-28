#ifndef FRAME_CLOCK_H
#define FRAME_CLOCK_H

/* Presentation pacing and an elapsed-time average, independent of pose clocks. */
typedef struct {
  double period, next_start, sample_start;
  unsigned presented;
  float fps;
} FrameClock;

static inline void frame_clock_reset(FrameClock *clock, double now) {
  *clock = (FrameClock){.period = 1.0 / 60.0, .sample_start = now};
}

static inline double frame_clock_delay(const FrameClock *clock, double now) {
  return clock->next_start > now ? clock->next_start - now : 0;
}

static inline void frame_clock_begin(FrameClock *clock, double now) {
  /* Missed frames do not trigger a burst of catch-up renders. */
  clock->next_start = now + clock->period;
}

static inline float frame_clock_present(FrameClock *clock, double now) {
  clock->presented++;
  double elapsed = now - clock->sample_start;
  if (elapsed >= 0.5) {
    clock->fps = (float)(clock->presented / elapsed);
    clock->presented = 0;
    clock->sample_start = now;
  }
  return clock->fps;
}

#endif
