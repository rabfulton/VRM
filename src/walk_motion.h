#ifndef WALK_MOTION_H
#define WALK_MOTION_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  WALK_NONE,
  WALK_TURN,
  WALK_TRAVEL,
  WALK_RETURN,
  WALK_DONE,
  WALK_CANCELLED
} WalkPhase;
typedef struct {
  WalkPhase phase;
  int64_t started_ns;
  double distance, travel_seconds, ramp, turn_seconds, return_seconds;
  double from_x, from_y, position, velocity, base_yaw, target_yaw, yaw;
  double progress, remaining;
} WalkMotion;

void walk_motion_begin(WalkMotion *m, int64_t now_ns, double x, double y,
                       double distance, double speed, double base_yaw,
                       double target_yaw);
void walk_motion_update(WalkMotion *m, int64_t now_ns);
void walk_motion_cancel(WalkMotion *m);
bool walk_motion_active(const WalkMotion *m);
double walk_motion_seconds(const WalkMotion *m);
double walk_motion_heading(double camera_azimuth, double distance);
double walk_motion_target(double x, double width, double work_x,
                          double work_width, double value, bool absolute);
const char *walk_motion_phase(const WalkMotion *m);
#endif
