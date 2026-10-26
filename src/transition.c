#include "transition.h"
#include <math.h>
#include <string.h>

static void cross(const float *a, const float *b, float *out) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}
static void multiply(const float *a, const float *b, float *out) {
    float q[4] = {
        a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    memcpy(out, q, sizeof(q));
}
static void rotate(const float *q, const float *v, float *out) {
    float c[3], cc[3];
    cross(q, v, c); cross(q, c, cc);
    for (int i=0;i<3;i++) out[i] = v[i] + 2*(q[3]*c[i]+cc[i]);
}
static void rotation_log(const float *q, float *out) {
    float length = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
    float sign = q[3] < 0 ? -1.0f : 1.0f;
    float factor = length > 1e-7f ? 2*atan2f(length, fabsf(q[3]))/length : 2;
    for (int i=0;i<3;i++) out[i] = q[i]*sign*factor;
}
static void rotation_exp(const float *v, float *q) {
    float angle = sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    float factor = angle > 1e-7f ? sinf(angle*0.5f)/angle : 0.5f;
    for (int i=0;i<3;i++) q[i] = v[i]*factor;
    q[3] = cosf(angle*0.5f);
}

void pose_velocity(const PoseTransform *previous, const PoseTransform *current,
                   float dt, PoseVelocity *velocity) {
    memset(velocity, 0, sizeof(*velocity));
    if (dt <= 1e-6f || dt > 0.1f) return; /* No extrapolation across pauses/stalls. */
    for (int i=0;i<3;i++) {
        velocity->translation[i] = (current->translation[i]-previous->translation[i])/dt;
        velocity->scale[i] = (current->scale[i]-previous->scale[i])/dt;
    }
    float inverse[4] = {-previous->rotation[0], -previous->rotation[1], -previous->rotation[2], previous->rotation[3]};
    float delta[4]; multiply(current->rotation, inverse, delta);
    rotation_log(delta, velocity->rotation);
    for (int i=0;i<3;i++) velocity->rotation[i] /= dt;
}

void pose_transition_init(PoseTransition *transition, const PoseTransform *source,
                          const PoseVelocity *source_velocity, const PoseTransform *target,
                          const PoseVelocity *target_velocity) {
    for (int i=0;i<3;i++) {
        transition->offset.translation[i] = source->translation[i]-target->translation[i];
        transition->offset.scale[i] = source->scale[i]-target->scale[i];
        transition->velocity.translation[i] = source_velocity->translation[i]-target_velocity->translation[i];
        transition->velocity.scale[i] = source_velocity->scale[i]-target_velocity->scale[i];
    }
    float inverse[4] = {-target->rotation[0], -target->rotation[1], -target->rotation[2], target->rotation[3]};
    float delta[4], rotated[3], omega[3], c[3], cc[3];
    multiply(source->rotation, inverse, delta);
    rotation_log(delta, transition->offset.rotation);
    rotate(delta, target_velocity->rotation, rotated);
    for (int i=0;i<3;i++) omega[i] = source_velocity->rotation[i]-rotated[i];
    /* Inverse SO(3) left Jacobian converts angular velocity to log-offset rate. */
    float *phi = transition->offset.rotation;
    float angle2 = phi[0]*phi[0]+phi[1]*phi[1]+phi[2]*phi[2];
    float angle = sqrtf(angle2);
    float a = angle2 < 1e-6f ? 1.0f/12.0f : (1-0.5f*angle/tanf(0.5f*angle))/angle2;
    cross(phi, omega, c); cross(phi, c, cc);
    for (int i=0;i<3;i++) transition->velocity.rotation[i] = omega[i]-0.5f*c[i]+a*cc[i];
}

void pose_transition_sample(const PoseTransition *transition, const PoseTransform *target,
                            float elapsed, float duration, PoseTransform *output) {
    *output = *target;
    if (duration <= 1e-6f || elapsed >= duration) return;
    float u = fmaxf(0, elapsed)/duration;
    float remaining=1-u, remaining3=remaining*remaining*remaining;
    /* Quintic offset decay: exact source pose/velocity, exact target at the end. */
    float position = remaining3*(1+3*u+6*u*u);
    float velocity = duration*u*remaining3*(1+3*u);
    float rotation[3], correction[4];
    for (int i=0;i<3;i++) {
        output->translation[i] += position*transition->offset.translation[i]+velocity*transition->velocity.translation[i];
        output->scale[i] += position*transition->offset.scale[i]+velocity*transition->velocity.scale[i];
        rotation[i] = position*transition->offset.rotation[i]+velocity*transition->velocity.rotation[i];
    }
    rotation_exp(rotation, correction);
    multiply(correction, target->rotation, output->rotation);
    float norm = 0;
    for (int i=0;i<4;i++) norm += output->rotation[i]*output->rotation[i];
    norm = sqrtf(norm);
    if (norm > 1e-8f) for (int i=0;i<4;i++) output->rotation[i] /= norm;
}
