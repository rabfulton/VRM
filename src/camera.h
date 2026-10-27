/*
 * VRM Overlay - Orbit Camera
 *
 * Simple orbit camera for viewing VRM models.
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <cglm/cglm.h>

typedef struct OrbitCamera {
  /* Spherical coordinates */
  float azimuth;  /* Horizontal angle (radians) */
  float polar;    /* Vertical angle (radians) */
  float distance; /* Distance from target */

  /* Target point */
  vec3 target;

  /* Projection parameters */
  float fov;    /* Field of view (radians) */
  float aspect; /* Aspect ratio */
  float near_clip;
  float far_clip;
  float viewport_height_scale; /* Expand transparent viewport without enlarging the avatar. */

  /* Computed matrices */
  mat4 view;
  mat4 proj;
} OrbitCamera;

/* Initialize camera with default values */
void camera_init(OrbitCamera *cam);

/* Update view and projection matrices */
void camera_update(OrbitCamera *cam);

/* Get camera world position */
void camera_get_position(const OrbitCamera *cam, vec3 out);

#endif /* CAMERA_H */
