/*
 * VRM Overlay - Orbit Camera Implementation
 */

#include "camera.h"
#include <math.h>

void camera_init(OrbitCamera *cam) {
  cam->azimuth = 0.0f;
  cam->polar = GLM_PI_2f; /* Look from side */
  cam->distance = 3.0f;

  glm_vec3_zero(cam->target);

  cam->fov = glm_rad(45.0f);
  cam->aspect = 1.0f;
  cam->near_clip = 0.1f;
  cam->far_clip = 100.0f;

  glm_mat4_identity(cam->view);
  glm_mat4_identity(cam->proj);
}

void camera_get_position(const OrbitCamera *cam, vec3 out) {
  /* Convert spherical to cartesian */
  float sin_polar = sinf(cam->polar);
  float cos_polar = cosf(cam->polar);
  float sin_azimuth = sinf(cam->azimuth);
  float cos_azimuth = cosf(cam->azimuth);

  out[0] = cam->target[0] + cam->distance * sin_polar * sin_azimuth;
  out[1] = cam->target[1] + cam->distance * cos_polar;
  out[2] = cam->target[2] + cam->distance * sin_polar * cos_azimuth;
}

void camera_update(OrbitCamera *cam) {
  /* Calculate camera position */
  vec3 eye;
  camera_get_position(cam, eye);

  /* Up vector */
  vec3 up = {0.0f, 1.0f, 0.0f};

  /* Build view matrix */
  glm_lookat(eye, cam->target, up, cam->view);

  /* Build projection matrix */
  glm_perspective(cam->fov, cam->aspect, cam->near_clip, cam->far_clip,
                  cam->proj);
}
