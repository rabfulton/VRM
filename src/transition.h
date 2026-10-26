#ifndef VRM_TRANSITION_H
#define VRM_TRANSITION_H
#ifdef __cplusplus
extern "C" {
#endif

/* Local-space poses. Quaternion order is x,y,z,w; angular velocity is rad/s. */
typedef struct { float translation[3], rotation[4], scale[3]; } PoseTransform;
typedef struct { float translation[3], rotation[3], scale[3]; } PoseVelocity;
typedef struct { PoseVelocity offset, velocity; } PoseTransition;

void pose_velocity(const PoseTransform *previous, const PoseTransform *current,
                   float dt, PoseVelocity *velocity);
void pose_transition_init(PoseTransition *transition, const PoseTransform *source,
                          const PoseVelocity *source_velocity, const PoseTransform *target,
                          const PoseVelocity *target_velocity);
void pose_transition_sample(const PoseTransition *transition, const PoseTransform *target,
                            float elapsed, float duration, PoseTransform *output);
#ifdef __cplusplus
}
#endif
#endif
