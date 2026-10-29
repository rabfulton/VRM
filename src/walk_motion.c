#include "walk_motion.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846

static double smooth(double x) {
  x = fmax(0, fmin(1, x));
  return x * x * (3 - 2 * x);
}
bool walk_motion_active(const WalkMotion *m) {
  return m->phase >= WALK_TURN && m->phase <= WALK_RETURN;
}
double walk_motion_seconds(const WalkMotion *m) {
  return m->turn_seconds + m->travel_seconds + m->return_seconds;
}
double walk_motion_heading(double camera_azimuth, double distance) {
  return (camera_azimuth - PI) * 180 / PI + (distance < 0 ? -90 : 90);
}
double walk_motion_target(double x, double width, double work_x,
                          double work_width, double value, bool absolute) {
  double right = fmax(work_x, work_x + work_width - width);
  if (absolute)
    return fmax(work_x, fmin(right, value - width * .5));
  return value < 0 ? fmin(x, fmax(work_x, x + value))
                   : fmax(x, fmin(right, x + value));
}
const char *walk_motion_phase(const WalkMotion *m) {
  const char *names[] = {"idle",         "turning",   "walking",
                         "turning_back", "completed", "cancelled"};
  return names[m->phase];
}
void walk_motion_begin(WalkMotion *m, int64_t now_ns, double x, double y,
                       double distance, double speed, double base_yaw,
                       double target_yaw) {
  memset(m, 0, sizeof(*m));
  m->started_ns = now_ns;
  m->from_x = x;
  m->from_y = y;
  m->distance = distance;
  m->position = x;
  m->base_yaw = base_yaw;
  m->yaw = base_yaw;
  double delta = remainder(target_yaw - base_yaw, 360);
  m->target_yaw = base_yaw + delta;
  m->turn_seconds = fabs(delta) > .5 ? .20 : 0;
  m->return_seconds = m->turn_seconds;
  m->ramp = .25;
  m->travel_seconds =
      fabs(distance) > .01
          ? fmax(2 * m->ramp, fabs(distance) / fmax(speed, 1) + m->ramp)
          : 0;
  if (!m->travel_seconds) {
    m->turn_seconds = m->return_seconds = 0;
    m->phase = WALK_DONE;
    m->progress = 1;
    return;
  }
  m->phase = m->turn_seconds > 0 ? WALK_TURN : WALK_TRAVEL;
  m->remaining = walk_motion_seconds(m);
}
void walk_motion_update(WalkMotion *m, int64_t now_ns) {
  if (!walk_motion_active(m))
    return;
  double t = fmax(0, (now_ns - m->started_ns) / 1e9),
         total = walk_motion_seconds(m);
  m->remaining = fmax(0, total - t);
  m->progress = fmin(1, t / total);
  m->velocity = 0;
  if (t < m->turn_seconds) {
    m->phase = WALK_TURN;
    m->yaw = m->base_yaw +
             (m->target_yaw - m->base_yaw) * smooth(t / m->turn_seconds);
    return;
  }
  t -= m->turn_seconds;
  if (t < m->travel_seconds) {
    m->phase = WALK_TRAVEL;
    m->yaw = m->target_yaw;
    double r = m->ramp, T = m->travel_seconds, v = m->distance / (T - r), d;
    if (t < r) {
      d = v * .5 * (t - r / PI * sin(PI * t / r));
      m->velocity = v * .5 * (1 - cos(PI * t / r));
    } else if (t <= T - r) {
      d = v * (t - r * .5);
      m->velocity = v;
    } else {
      double tail = T - t;
      d = m->distance - v * .5 * (tail - r / PI * sin(PI * tail / r));
      m->velocity = v * .5 * (1 - cos(PI * tail / r));
    }
    m->position = m->from_x + d;
    return;
  }
  m->position = m->from_x + m->distance;
  t -= m->travel_seconds;
  if (t < m->return_seconds) {
    m->phase = WALK_RETURN;
    m->yaw = m->target_yaw +
             (m->base_yaw - m->target_yaw) * smooth(t / m->return_seconds);
    return;
  }
  m->phase = WALK_DONE;
  m->yaw = m->base_yaw;
  m->remaining = 0;
  m->progress = 1;
}
void walk_motion_cancel(WalkMotion *m) {
  if (walk_motion_active(m)) {
    m->phase = WALK_CANCELLED;
    m->yaw = m->base_yaw;
    m->velocity = 0;
    m->remaining = 0;
  }
}
