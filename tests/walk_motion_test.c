#include "walk_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define NS(seconds) ((int64_t)llround((seconds) * 1e9))

int main(void) {
  // Absolute x addresses the viewport centre; relative deltas address travel.
  assert(walk_motion_target(300, 200, 0, 1920, 900, true) == 800);
  assert(walk_motion_target(0, 200, 0, 5120, 4000, true) == 3900);
  assert(walk_motion_target(300, 200, 0, 1920, 400, true) == 300);
  assert(walk_motion_target(300, 200, 0, 1920, 400.5, true) == 300.5);
  assert(walk_motion_target(300, 200, 0, 1920, -100, true) == 0);
  assert(walk_motion_target(300, 200, 0, 1920, 3000, true) == 1720);
  assert(walk_motion_target(-1000, 200, -1920, 1920, -1200, true) == -1300);
  assert(walk_motion_target(300, 200, 0, 1920, -100, false) == 200);
  assert(walk_motion_target(-50, 200, 0, 1920, -100, false) == -50);
  assert(walk_motion_target(1800, 200, 0, 1920, 100, false) == 1800);
  assert(walk_motion_target(0, 200, 0, 100, 1000, true) == 0);
  /* The controller loads a neutral gait: the renderer alone turns a quarter
   * circle, in the requested direction, then retraces it back to idle. */
  for (int direction = -1; direction <= 1; direction += 2) {
    WalkMotion m;
    double target = walk_motion_heading(3.14159265358979323846, direction);
    assert(fabs(target - direction * 90) < 1e-8);
    walk_motion_begin(&m, 0, 500, 0, direction * 100, 150, 0, target);
    double previous = 0, travelled = 0;
    for (int i = 0; i <= 100; i++) {
      walk_motion_update(&m, NS(m.turn_seconds * i / 100));
      assert(direction * (m.yaw - previous) >= -1e-8);
      travelled += fabs(m.yaw - previous);
      previous = m.yaw;
    }
    assert(fabs(travelled - 90) < 1e-6);
    double returning = m.turn_seconds + m.travel_seconds;
    for (int i = 0; i <= 100; i++) {
      walk_motion_update(&m, NS(returning + m.return_seconds * i / 100));
      assert(direction * (m.yaw - previous) <= 1e-8);
      travelled += fabs(m.yaw - previous);
      previous = m.yaw;
    }
    assert(fabs(travelled - 180) < 1e-6);
  }
  for (int direction = -1; direction <= 1; direction += 2) {
    WalkMotion m;
    walk_motion_begin(&m, NS(10), 500, 600, direction * 240, 150, 350,
                      direction < 0 ? 180 : 0);
    assert(walk_motion_active(&m));
    double total = walk_motion_seconds(&m), previous = 500;
    for (int i = 0; i <= 1000; i++) {
      walk_motion_update(&m, NS(10 + total * i / 1000));
      assert(direction * (m.position - previous) >= -1e-7);
      assert(m.progress >= 0 && m.progress <= 1 && m.remaining >= 0);
      assert(fabs(m.yaw - 350) <= 180);
      previous = m.position;
    }
    walk_motion_update(&m, NS(10 + total + .01));
    assert(m.phase == WALK_DONE && !walk_motion_active(&m));
    assert(fabs(m.position - (500 + direction * 240)) < 1e-6);
    assert(m.yaw == 350 && m.progress == 1 && m.remaining == 0 &&
           m.velocity == 0);
    WalkMotion coarse;
    walk_motion_begin(&coarse, NS(10), 500, 600, direction * 240, 150, 350,
                      direction < 0 ? 180 : 0);
    walk_motion_update(&coarse, NS(10 + total + 2));
    assert(coarse.position == m.position && coarse.phase == WALK_DONE);
  }
  WalkMotion m;
  walk_motion_begin(&m, 0, 20, 0, 100, 100, -900, 0);
  assert(fabs(m.target_yaw - m.base_yaw) <= 180);
  walk_motion_update(&m, NS(.6));
  double stopped = m.position;
  walk_motion_cancel(&m);
  walk_motion_update(&m, NS(100));
  assert(m.phase == WALK_CANCELLED && m.position == stopped && m.velocity == 0);
  walk_motion_begin(&m, 0, 20, 0, 0, 100, 0, 180);
  assert(m.phase == WALK_DONE && m.position == 20 &&
         walk_motion_seconds(&m) == 0);
  walk_motion_begin(&m, 0, 20, 0, 1, 100, 0, 0);
  walk_motion_update(&m, NS(.25));
  assert(fabs(m.position - 20.5) < 1e-8);
  puts("walk motion tests passed");
}
