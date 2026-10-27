/*
 * VRM spring chains - fixed-step simulation with garment collision extensions
 */

#include "spring_bones.h"
#include "gltf_loader.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global tuning multipliers */
float g_spring_stiffness = 1.0f;
float g_spring_drag = 1.0f;
float g_spring_gravity = 0.1f;
int g_spring_cloth_mode = 0;
float g_spring_cloth_stiffness = 0.35f;
float g_spring_cloth_drag = 1.2f;
float g_spring_cloth_gravity = 0.1f;
float g_spring_cloth_taper = 0.6f;
int g_spring_debug_collisions = 0;
int g_spring_garment_collisions = 1;

/* Vector math */
static inline void v3_sub(const float *a, const float *b, float *out) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}
static inline void v3_add(const float *a, const float *b, float *out) {
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}
static inline void v3_scale(const float *v, float s, float *out) {
    out[0] = v[0] * s; out[1] = v[1] * s; out[2] = v[2] * s;
}
static inline float v3_dot(const float *a, const float *b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static inline float v3_len(const float *v) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}
static inline float v3_len_sq(const float *v) {
    return v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
}
static inline void v3_normalize(float *v) {
    float len = v3_len(v);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

/* Get world position from matrix */
static inline void get_world_pos(const float *m, float *p) {
    p[0] = m[12]; p[1] = m[13]; p[2] = m[14];
}

static void mat4_mul(const float *a, const float *b, float *out) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
}

static int mat4_inverse(const float *m, float *out) {
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
              m[4]  * m[11] * m[14] +
              m[8]  * m[6]  * m[15] -
              m[8]  * m[7]  * m[14] -
              m[12] * m[6]  * m[11] +
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] -
               m[8]  * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
              m[1]  * m[11] * m[14] +
              m[9]  * m[2] * m[15] -
              m[9]  * m[3] * m[14] -
              m[13] * m[2] * m[11] +
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
              m[0]  * m[11] * m[13] +
              m[8]  * m[1] * m[15] -
              m[8]  * m[3] * m[13] -
              m[12] * m[1] * m[11] +
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
              m[0]  * m[7] * m[14] +
              m[4]  * m[2] * m[15] -
              m[4]  * m[3] * m[14] -
              m[12] * m[2] * m[7] +
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
               m[0]  * m[6] * m[13] +
               m[4]  * m[1] * m[14] -
               m[4]  * m[2] * m[13] -
               m[12] * m[1] * m[6] +
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
              m[1] * m[7] * m[10] +
              m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] -
              m[9] * m[2] * m[7] +
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
               m[0] * m[7] * m[9] +
               m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] -
               m[8] * m[1] * m[7] +
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-8f) return 0;
    det = 1.0f / det;

    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
    return 1;
}

/* Transform point by matrix */
static void transform_point(const float *m, const float *local, float *world) {
    world[0] = m[0]*local[0] + m[4]*local[1] + m[8]*local[2] + m[12];
    world[1] = m[1]*local[0] + m[5]*local[1] + m[9]*local[2] + m[13];
    world[2] = m[2]*local[0] + m[6]*local[1] + m[10]*local[2] + m[14];
}

static void transform_vec(const float *m, const float *local, float *world) {
    world[0] = m[0]*local[0] + m[4]*local[1] + m[8]*local[2];
    world[1] = m[1]*local[0] + m[5]*local[1] + m[9]*local[2];
    world[2] = m[2]*local[0] + m[6]*local[1] + m[10]*local[2];
}

static void build_local_matrix(const float *t, const float *r, const float *s, float *out) {
    float qx = r[0], qy = r[1], qz = r[2], qw = r[3];
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    out[0] = (1 - 2*(yy + zz)) * s[0];
    out[1] = 2*(xy + wz) * s[0];
    out[2] = 2*(xz - wy) * s[0];
    out[3] = 0;

    out[4] = 2*(xy - wz) * s[1];
    out[5] = (1 - 2*(xx + zz)) * s[1];
    out[6] = 2*(yz + wx) * s[1];
    out[7] = 0;

    out[8] = 2*(xz + wy) * s[2];
    out[9] = 2*(yz - wx) * s[2];
    out[10] = (1 - 2*(xx + yy)) * s[2];
    out[11] = 0;

    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1;
}

static void update_node_transform(GltfModel *model, int node_idx, const float *parent_matrix) {
    if (node_idx < 0 || node_idx >= model->node_count) return;
    GltfNode *node = &model->nodes[node_idx];
    float local[16];
    build_local_matrix(node->translation, node->rotation, node->scale, local);
    mat4_mul(parent_matrix, local, node->world_matrix);
    for (int i = 0; i < node->child_count; i++) {
        update_node_transform(model, node->children[i], node->world_matrix);
    }
}

/* Quaternion operations */
static void quat_mul(const float *a, const float *b, float *out) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

static void quat_rotate_vec3(const float *q, const float *v, float *out) {
    float qv[3] = {q[0], q[1], q[2]};
    float uv[3], uuv[3];
    uv[0] = qv[1]*v[2] - qv[2]*v[1];
    uv[1] = qv[2]*v[0] - qv[0]*v[2];
    uv[2] = qv[0]*v[1] - qv[1]*v[0];
    uuv[0] = qv[1]*uv[2] - qv[2]*uv[1];
    uuv[1] = qv[2]*uv[0] - qv[0]*uv[2];
    uuv[2] = qv[0]*uv[1] - qv[1]*uv[0];
    out[0] = v[0] + 2.0f * (q[3]*uv[0] + uuv[0]);
    out[1] = v[1] + 2.0f * (q[3]*uv[1] + uuv[1]);
    out[2] = v[2] + 2.0f * (q[3]*uv[2] + uuv[2]);
}

static void mat4_to_quat(const float *m, float *q) {
    float trace = m[0] + m[5] + m[10];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        q[3] = 0.25f / s;
        q[0] = (m[6] - m[9]) * s;
        q[1] = (m[8] - m[2]) * s;
        q[2] = (m[1] - m[4]) * s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        float s = 2.0f * sqrtf(1.0f + m[0] - m[5] - m[10]);
        q[3] = (m[6] - m[9]) / s; q[0] = 0.25f * s;
        q[1] = (m[4] + m[1]) / s; q[2] = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        float s = 2.0f * sqrtf(1.0f + m[5] - m[0] - m[10]);
        q[3] = (m[8] - m[2]) / s; q[0] = (m[4] + m[1]) / s;
        q[1] = 0.25f * s; q[2] = (m[9] + m[6]) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + m[10] - m[0] - m[5]);
        q[3] = (m[1] - m[4]) / s; q[0] = (m[8] + m[2]) / s;
        q[1] = (m[9] + m[6]) / s; q[2] = 0.25f * s;
    }
    float len = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (len > 1e-6f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* fromToQuaternion - rotation from vector 'from' to vector 'to' */
static void quat_from_to(const float *from, const float *to, float *q) {
    float fn[3] = {from[0], from[1], from[2]};
    float tn[3] = {to[0], to[1], to[2]};
    v3_normalize(fn); v3_normalize(tn);
    float d = v3_dot(fn, tn);
    if (d > 0.9999f) { q[0]=q[1]=q[2]=0; q[3]=1; return; }
    if (d < -0.9999f) {
        /* A 180-degree rotation needs an axis perpendicular to this bone. */
        float axis[3];
        if (fabsf(fn[0]) < 0.8f) { axis[0]=0; axis[1]=fn[2]; axis[2]=-fn[1]; }
        else { axis[0]=-fn[2]; axis[1]=0; axis[2]=fn[0]; }
        v3_normalize(axis);
        memcpy(q,axis,3*sizeof(float)); q[3]=0; return;
    }
    float axis[3] = { fn[1]*tn[2]-fn[2]*tn[1], fn[2]*tn[0]-fn[0]*tn[2], fn[0]*tn[1]-fn[1]*tn[0] };
    q[0]=axis[0]; q[1]=axis[1]; q[2]=axis[2]; q[3]=1.0f+d;
    float len = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (len > 1e-6f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* Runtime policy augments named garments; authored collider groups stay intact. */
typedef struct {
    SpringCollider collider;
    int group;
    int arm;
    float previous_head[3], previous_tail[3], head[3], tail[3];
    float previous_radius, radius;
} RuntimeCollider;

struct SpringRuntime {
    RuntimeCollider *colliders;
    int collider_count;
    unsigned char *garments;
    double accumulator;
    SpringStats stats;
};

static float maximum_scale(const float *matrix) {
    float maximum = 0;
    for (int col=0;col<3;col++) {
        float length = v3_len(matrix+col*4);
        if (length > maximum) maximum = length;
    }
    return maximum;
}

static int humanoid_node(const GltfModel *model, const char *name) {
    for (int i=0;i<model->humanoid_bone_count;i++)
        if (!strcmp(model->humanoid_names[i], name)) return model->humanoid_bones[i];
    return -1;
}

static int arm_node(const GltfModel *model, int node) {
    const char *names[] = {"leftUpperArm", "leftLowerArm", "leftHand", "rightUpperArm", "rightLowerArm", "rightHand"};
    for (int i=0;i<6;i++) if (node >= 0 && node == humanoid_node(model, names[i])) return 1;
    return 0;
}

static int garment_chain(const GltfModel *model, const SpringBoneGroup *group) {
    if (!group->bones || group->bone_count == 0) return 0;
    int node = group->bones[0];
    if (node < 0 || node >= model->node_count) return 0;
    char name[sizeof(model->nodes[node].name)];
    size_t i=0;
    for (;i+1<sizeof(name) && model->nodes[node].name[i];i++)
        name[i] = (char)tolower((unsigned char)model->nodes[node].name[i]);
    name[i] = 0;
    return strstr(name, "skirt") || strstr(name, "dress") || strstr(name, "coat") || strstr(name, "cloth");
}

static void capture_colliders(GltfModel *model, int reset) {
    struct SpringRuntime *runtime = model->spring_runtime;
    for (int i=0;i<runtime->collider_count;i++) {
        RuntimeCollider *col = &runtime->colliders[i];
        const float *matrix = model->nodes[col->collider.node].world_matrix;
        transform_point(matrix, col->collider.offset, col->head);
        if (col->collider.is_capsule) transform_point(matrix, col->collider.tail, col->tail);
        else memcpy(col->tail, col->head, sizeof(col->tail));
        col->radius = col->collider.radius * maximum_scale(matrix);
        if (reset) {
            memcpy(col->previous_head, col->head, sizeof(col->head));
            memcpy(col->previous_tail, col->tail, sizeof(col->tail));
            col->previous_radius = col->radius;
        }
    }
}

static void create_spring_runtime(GltfModel *model) {
    if (model->spring_runtime) return;
    struct SpringRuntime *rt = calloc(1, sizeof(*rt));
    if (!rt) return;
    int authored_count = 0;
    for (int g=0;g<model->spring_collider_count;g++) authored_count += model->spring_colliders[g].collider_count;
    rt->colliders = calloc((size_t)authored_count + 6, sizeof(*rt->colliders));
    rt->garments = calloc((size_t)model->spring_group_count, 1);
    if (!rt->colliders || !rt->garments) { free(rt->colliders); free(rt->garments); free(rt); return; }
    for (int g=0;g<model->spring_group_count;g++) {
        rt->garments[g] = (unsigned char)garment_chain(model, &model->spring_groups[g]);
        rt->stats.garment_chains += rt->garments[g] != 0;
    }
    for (int g=0;g<model->spring_collider_count;g++) {
        SpringColliderGroup *group = &model->spring_colliders[g];
        for (int c=0;c<group->collider_count;c++) {
            SpringCollider col = group->colliders[c];
            if (col.node < 0 || col.node >= model->node_count) continue;
            RuntimeCollider *dst = &rt->colliders[rt->collider_count++];
            dst->collider = col; dst->group = g; dst->arm = arm_node(model, col.node);
            rt->stats.arm_colliders += dst->arm;
        }
    }
    /* Missing arm volumes get conservative humanoid capsule proxies. */
    const char *starts[] = {"leftUpperArm", "leftLowerArm", "leftHand", "rightUpperArm", "rightLowerArm", "rightHand"};
    const char *ends[] = {"leftLowerArm", "leftHand", "leftMiddleProximal", "rightLowerArm", "rightHand", "rightMiddleProximal"};
    for (int i=0;i<6;i++) {
        int start = humanoid_node(model, starts[i]), end = humanoid_node(model, ends[i]);
        if (start < 0 || start >= model->node_count || end < 0 || end >= model->node_count) continue;
        int exists = 0;
        for (int c=0;c<rt->collider_count;c++) if (rt->colliders[c].collider.node == start && rt->colliders[c].arm) exists = 1;
        if (exists) continue;
        float inverse[16], world_tail[3], local_tail[3];
        if (!mat4_inverse(model->nodes[start].world_matrix, inverse)) continue;
        get_world_pos(model->nodes[end].world_matrix, world_tail);
        transform_point(inverse, world_tail, local_tail);
        float length = v3_len(local_tail);
        if (length < 1e-5f) continue;
        RuntimeCollider *dst = &rt->colliders[rt->collider_count++];
        dst->collider.node = start; dst->collider.is_capsule = 1;
        dst->collider.radius = length * (i%3 == 2 ? 0.4f : 0.16f);
        memcpy(dst->collider.tail, local_tail, sizeof(local_tail));
        dst->group = -1; dst->arm = 1;
        rt->stats.arm_colliders++;
        rt->stats.generated_arm_colliders++;
    }
    model->spring_runtime = rt;
    capture_colliders(model, 1);
}

/* Closest points of two segments, including point/sphere degeneracies. */
static float segment_distance(const float *p1, const float *q1, const float *p2,
                              const float *q2, float *fraction, float *on_body) {
    float d1[3], d2[3], r[3];
    v3_sub(q1,p1,d1); v3_sub(q2,p2,d2); v3_sub(p1,p2,r);
    float a=v3_dot(d1,d1), e=v3_dot(d2,d2), f=v3_dot(d2,r), s=0, t=0;
    if (a > 1e-12f) {
        float c=v3_dot(d1,r);
        if (e <= 1e-12f) s=fminf(1,fmaxf(0,-c/a));
        else {
            float b=v3_dot(d1,d2), denominator=a*e-b*b;
            if (denominator > 1e-12f) s=fminf(1,fmaxf(0,(b*f-c*e)/denominator));
            t=(b*s+f)/e;
            if (t<0) { t=0; s=fminf(1,fmaxf(0,-c/a)); }
            else if (t>1) { t=1; s=fminf(1,fmaxf(0,(b-c)/a)); }
        }
    } else if (e>1e-12f) t=fminf(1,fmaxf(0,f/e));
    float delta[3];
    for (int i=0;i<3;i++) { on_body[i]=p2[i]+d2[i]*t; delta[i]=p1[i]+d1[i]*s-on_body[i]; }
    *fraction=s;
    return v3_len(delta);
}

static const float garment_bend_cosine=0.8191520443f;
static const float garment_bend_sine=0.5735764364f; /* 35 degrees */

/* Project contact onto the bone-length sphere, rather than undoing separation
 * with a subsequent length projection. Contacts at a fixed root can be infeasible. */
static int separate_contact(const float *root, float *tail, float length,
                            float fraction, const float *center, float radius,
                            const float *garment_rest) {
    float distance_vector[3], direction[3], axis[3], tangent[3];
    v3_sub(center,root,distance_vector);
    float d=v3_len(distance_vector), contact_length=length*fraction;
    if (d<1e-7f || contact_length<1e-6f) return 0;
    float k=(contact_length*contact_length+d*d-radius*radius)/(2*contact_length*d);
    if (k < -1 || k > 1) return 0;
    v3_scale(distance_vector,1/d,axis);
    v3_sub(tail,root,direction); v3_normalize(direction);
    float dot=v3_dot(direction,axis);
    for (int i=0;i<3;i++) tangent[i]=direction[i]-axis[i]*dot;
    if (v3_len(tangent)<1e-6f) {
        int coordinate=fabsf(axis[0])<0.8f ? 0 : 1;
        for (int i=0;i<3;i++) tangent[i]=(i==coordinate ? 1.0f : 0)-axis[i]*axis[coordinate];
    }
    v3_normalize(tangent);
    float lateral=sqrtf(fmaxf(0,1-k*k));
    float result[3];
    for (int i=0;i<3;i++) result[i]=axis[i]*k+tangent[i]*lateral;
    if (garment_rest && v3_dot(result,garment_rest)<garment_bend_cosine) {
        /* Solve the contact circle and bend cone together. Alternating two
         * independent projections can leave a feasible contact penetrating
         * at the cone rim and cause jitter when a thigh moves across it. */
        float alignment=v3_dot(axis,garment_rest);
        float denominator=1-alignment*alignment;
        float cross[3]={axis[1]*garment_rest[2]-axis[2]*garment_rest[1],
                        axis[2]*garment_rest[0]-axis[0]*garment_rest[2],
                        axis[0]*garment_rest[1]-axis[1]*garment_rest[0]};
        float remaining=-1;
        if (denominator>1e-6f) {
            float a=(k-alignment*garment_bend_cosine)/denominator;
            float b=(garment_bend_cosine-alignment*k)/denominator;
            for(int i=0;i<3;i++)result[i]=a*axis[i]+b*garment_rest[i];
            remaining=1-v3_dot(result,result);
        }
        if (remaining>=-1e-6f) {
            v3_normalize(cross);
            float side=v3_dot(cross,direction)<0 ? -1 : 1;
            for(int i=0;i<3;i++)result[i]+=side*sqrtf(fmaxf(0,remaining))*cross[i];
        } else {
            /* No direction satisfies both constraints. Keep the garment's
             * shape and maximize separation within its permitted bend cone. */
            for(int i=0;i<3;i++)tangent[i]=alignment*garment_rest[i]-axis[i];
            if(v3_len(tangent)<1e-6f) {
                int coordinate=fabsf(garment_rest[0])<0.8f ? 0 : 1;
                for(int i=0;i<3;i++)tangent[i]=(i==coordinate ? 1.0f : 0)-garment_rest[i]*garment_rest[coordinate];
            }
            v3_normalize(tangent);
            for(int i=0;i<3;i++)result[i]=garment_bend_cosine*garment_rest[i]+garment_bend_sine*tangent[i];
        }
        v3_normalize(result);
    }
    for (int i=0;i<3;i++) tail[i]=root[i]+length*result[i];
    return 1;
}

static int collider_applies(const SpringBoneGroup *group, const RuntimeCollider *col, int garment) {
    if (garment && g_spring_garment_collisions && col->arm) return 1;
    if (col->group >= 0) for (int i=0;i<group->collider_group_count;i++) if (group->collider_groups[i] == col->group) return 1;
    return 0;
}

/* Garment chains model connected fabric, rather than independently hinged
 * strands. Bound each segment against its unsimulated attachment-space rest
 * direction so successive joints cannot accumulate into an inside-out fold.
 * This reference follows the animated hips/attachment, including lying poses. */
static int limit_garment_bend(const float *root, float *tail, float length,
                              const float *rest_direction) {
    if (!rest_direction) return 0;
    float direction[3],tangent[3];
    v3_sub(tail,root,direction);v3_normalize(direction);
    float dot=v3_dot(direction,rest_direction);
    if (dot>=garment_bend_cosine) return 0;
    for(int k=0;k<3;k++)tangent[k]=direction[k]-dot*rest_direction[k];
    if(v3_len(tangent)<1e-6f) {
        int axis=fabsf(rest_direction[0])<0.8f ? 0 : 1;
        for(int k=0;k<3;k++)tangent[k]=(k==axis ? 1.0f : 0)-rest_direction[k]*rest_direction[axis];
    }
    v3_normalize(tangent);
    for(int k=0;k<3;k++)tail[k]=root[k]+length*(garment_bend_cosine*rest_direction[k]+garment_bend_sine*tangent[k]);
    return 1;
}

static int solve_contacts(GltfModel *model, SpringBoneGroup *group, SpringJoint *joint,
                          const float *root, float *tail, int garment, float alpha,
                          const float *garment_rest_direction) {
    struct SpringRuntime *rt=model->spring_runtime;
    int contacts=limit_garment_bend(root,tail,joint->bone_length,garment_rest_direction);
    float hit=joint->hit_radius*maximum_scale(model->nodes[joint->node].world_matrix);
    for (int iteration=0;iteration<12;iteration++) {
        int corrected=0;
        for (int c=0;c<rt->collider_count;c++) {
            RuntimeCollider *col=&rt->colliders[c];
            if (!collider_applies(group,col,garment)) continue;
            float a[3], b[3], on_body[3], fraction;
            for (int i=0;i<3;i++) {
                a[i]=col->previous_head[i]+alpha*(col->head[i]-col->previous_head[i]);
                b[i]=col->previous_tail[i]+alpha*(col->tail[i]-col->previous_tail[i]);
            }
            float radius=hit+col->previous_radius+alpha*(col->radius-col->previous_radius);
            /* Garments need coverage between particles for thighs as well as
             * arms. Keep anchored-root overlaps feasible with the tip fallback
             * below; hair retains the authored tail-particle convention. */
            int shaft=garment;
            float distance=shaft ? segment_distance(root,tail,a,b,&fraction,on_body) :
                                  segment_distance(tail,tail,a,b,&fraction,on_body);
            if (!shaft) fraction=1;
            if (distance+1e-5f >= radius) continue;
            /* An anchored shaft cannot escape a volume containing its root.
             * Projecting a point farther along it just flips the bone between
             * incompatible directions as the closest point changes. Fall back
             * to the movable tip and keep the shaft overlap in diagnostics. */
            if (shaft) {
                float root_fraction,root_body[3];
                if (segment_distance(root,root,a,b,&root_fraction,root_body)<radius) {
                    distance=segment_distance(tail,tail,a,b,&fraction,on_body);
                    fraction=1;
                    if (distance+1e-5f >= radius) continue;
                }
            }
            if (iteration == 0) contacts++;
            corrected |= separate_contact(root,tail,joint->bone_length,fraction,on_body,radius+1e-5f,
                                           garment_rest_direction);
            corrected |= limit_garment_bend(root,tail,joint->bone_length,garment_rest_direction);
        }
        if (!corrected) break;
    }
    for (int c=0;c<rt->collider_count;c++) {
        RuntimeCollider *col=&rt->colliders[c];
        int applies=collider_applies(group,col,garment);
        if (!applies && !(garment && col->arm)) continue;
        float a[3],b[3],body[3],fraction;
        for (int i=0;i<3;i++) {
            a[i]=col->previous_head[i]+alpha*(col->head[i]-col->previous_head[i]);
            b[i]=col->previous_tail[i]+alpha*(col->tail[i]-col->previous_tail[i]);
        }
        float radius=hit+col->previous_radius+alpha*(col->radius-col->previous_radius);
        float distance=garment ? segment_distance(root,tail,a,b,&fraction,body) : segment_distance(tail,tail,a,b,&fraction,body);
        float penetration=fmaxf(0,radius-distance);
        if (garment && col->arm && penetration>rt->stats.garment_arm_penetration) rt->stats.garment_arm_penetration=penetration;
        if (!applies) continue;
        if (penetration > rt->stats.max_penetration) rt->stats.max_penetration=penetration;
        if (penetration>0.001f) rt->stats.unresolved_contacts++;
    }
    rt->stats.contacts += contacts;
    return contacts;
}

/* Initialize spring bones per spec */
void spring_bones_init(GltfModel *model) {
    if (!model || model->spring_group_count == 0) return;

    int total_joints = 0;
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (group->bone_count < 2) continue;

        float bone_len_sum = 0.0f;
        int bone_len_count = 0;

        /* Per spec: N joints create N-1 joint pairs */
        if (!group->joints) {
            group->joint_count = group->bone_count - 1;
            group->joints = (SpringJoint *)calloc(group->joint_count, sizeof(SpringJoint));
        }

        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            int head_idx = group->bones[i];
            int tail_idx = group->bones[i + 1];
            joint->node = head_idx;

            if (head_idx < 0 || head_idx >= model->node_count ||
                tail_idx < 0 || tail_idx >= model->node_count) continue;

            GltfNode *head = &model->nodes[head_idx];
            GltfNode *tail = &model->nodes[tail_idx];

            /* Store initial local rotation and matrix */
            memcpy(joint->initial_local_rotation, head->base_rotation, 4*sizeof(float));
            build_local_matrix(head->base_translation, head->base_rotation, head->base_scale,
                               joint->initial_local_matrix);

            /* Calculate bone axis (direction to tail in local space) */
            float head_pos[3], tail_pos[3];
            get_world_pos(head->world_matrix, head_pos);
            get_world_pos(tail->world_matrix, tail_pos);

            /* Bone length in world space */
            float diff[3];
            v3_sub(tail_pos, head_pos, diff);
            joint->bone_length = v3_len(diff);
            if (joint->bone_length < 0.001f) joint->bone_length = 0.07f;
            bone_len_sum += joint->bone_length;
            bone_len_count++;

            /* Bone axis: direction to tail in head's local space */
            float head_inv[16];
            float local_tail[3] = {0, 0, 0};
            if (mat4_inverse(head->world_matrix, head_inv)) {
                transform_point(head_inv, tail_pos, local_tail);
            } else {
                local_tail[0] = tail->translation[0];
                local_tail[1] = tail->translation[1];
                local_tail[2] = tail->translation[2];
            }
            joint->bone_axis[0] = local_tail[0];
            joint->bone_axis[1] = local_tail[1];
            joint->bone_axis[2] = local_tail[2];
            float axis_len = v3_len(joint->bone_axis);
            if (axis_len > 1e-6f) {
                v3_scale(joint->bone_axis, 1.0f/axis_len, joint->bone_axis);
            } else {
                joint->bone_axis[0] = 0; joint->bone_axis[1] = -1; joint->bone_axis[2] = 0;
            }

            /* Initialize tail positions */
            memcpy(joint->current_tail, tail_pos, 3*sizeof(float));
            memcpy(joint->prev_tail, tail_pos, 3*sizeof(float));

            total_joints++;
        }

        group->avg_bone_length = bone_len_count > 0 ? bone_len_sum / (float)bone_len_count : 0.0f;
    }
    create_spring_runtime(model);
    printf("Spring bones initialized: %d groups, %d joint pairs\n",
           model->spring_group_count, total_joints);
}

/* Reset to rest pose */
void spring_bones_reset(GltfModel *model) {
    if (!model) return;
    if (model->spring_runtime) model->spring_runtime->accumulator = 0;

    /* Restore all spring rotations before refreshing their world-space tails. */
    for (int g=0;g<model->spring_group_count;g++) {
        SpringBoneGroup *group=&model->spring_groups[g];
        for (int j=0;j<group->joint_count;j++) {
            int node=group->joints[j].node;
            if (node>=0 && node<model->node_count) memcpy(model->nodes[node].rotation,model->nodes[node].base_rotation,4*sizeof(float));
        }
    }
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for (int n=0;n<model->node_count;n++) if (model->nodes[n].parent_index<0) update_node_transform(model,n,identity);
    if (model->spring_runtime) capture_colliders(model,1);
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints) continue;

        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node < 0 || joint->node >= model->node_count) continue;

            GltfNode *head = &model->nodes[joint->node];
            int tail_idx = group->bones[i + 1];
            if (tail_idx < 0 || tail_idx >= model->node_count) continue;
            GltfNode *tail = &model->nodes[tail_idx];

            float tail_pos[3];
            get_world_pos(tail->world_matrix, tail_pos);
            memcpy(joint->current_tail, tail_pos, 3*sizeof(float));
            memcpy(joint->prev_tail, tail_pos, 3*sizeof(float));

            memcpy(head->rotation, head->base_rotation, 4*sizeof(float));
        }
    }
}

/* Advance spring chains and resolve contacts at the fixed simulation step. */
static void spring_step(GltfModel *model, float dt, float alpha) {
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints || group->joint_count == 0) continue;

        int cloth_chain = model->spring_runtime->garments[g];
        float attachment_rest_rotation[4]={0,0,0,1};
        if (cloth_chain) {
            int parent=model->nodes[group->bones[0]].parent_index;
            if(parent>=0 && parent<model->node_count)
                mat4_to_quat(model->nodes[parent].world_matrix,attachment_rest_rotation);
        }

        /* Process joints root to tip (spec: calculation order) */
        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node < 0 || joint->node >= model->node_count) continue;

            GltfNode *node = &model->nodes[joint->node];
            float garment_rest_direction[3];
            if(cloth_chain) {
                float rotation[4];
                quat_mul(attachment_rest_rotation,joint->initial_local_rotation,rotation);
                memcpy(attachment_rest_rotation,rotation,sizeof(rotation));
                quat_rotate_vec3(rotation,joint->bone_axis,garment_rest_direction);
                v3_normalize(garment_rest_direction);
            }

            /* Get parameters */
            float drag = joint->drag_force;
            float stiffness = joint->stiffness;
            float grav_power = joint->gravity_power;
            float *grav_dir = joint->gravity_dir;

            /* Apply global tuning */
            drag *= g_spring_drag;
            stiffness *= g_spring_stiffness;
            grav_power *= g_spring_gravity;
            if (g_spring_cloth_mode && cloth_chain) {
                float t = (group->joint_count > 1) ? (float)i / (float)(group->joint_count - 1) : 0.0f;
                float taper = 1.0f - g_spring_cloth_taper * t;
                float length_scale = 1.0f;
                if (group->avg_bone_length > 1e-6f) {
                    length_scale = sqrtf(joint->bone_length / group->avg_bone_length);
                    if (length_scale < 0.5f) length_scale = 0.5f;
                    if (length_scale > 1.5f) length_scale = 1.5f;
                }
                stiffness *= g_spring_cloth_stiffness;
                stiffness *= taper * length_scale;
                drag *= g_spring_cloth_drag;
                grav_power *= g_spring_cloth_gravity;
            }

            float world_pos[3];
            get_world_pos(node->world_matrix, world_pos);

            /* Get parent world rotation */
            float parent_rot[4] = {0, 0, 0, 1};
            if (node->parent_index >= 0 && node->parent_index < model->node_count) {
                mat4_to_quat(model->nodes[node->parent_index].world_matrix, parent_rot);
            }

            /* === STEP 1: Inertia calculation (per spec) === */

            /* Calculate rest direction in world space */
            float combined_rot[4];
            quat_mul(parent_rot, joint->initial_local_rotation, combined_rot);
            float rest_dir[3];
            quat_rotate_vec3(combined_rot, joint->bone_axis, rest_dir);

            /* Inertia: (currentTail - prevTail) * (1.0 - dragForce) */
            float inertia[3];
            if (group->center >= 0 && group->center < model->node_count) {
                const float *center_world = model->nodes[group->center].world_matrix;
                float center_inv[16];
                if (mat4_inverse(center_world, center_inv)) {
                    float curr_center[3], prev_center[3], inertia_center[3];
                    transform_point(center_inv, joint->current_tail, curr_center);
                    transform_point(center_inv, joint->prev_tail, prev_center);
                    v3_sub(curr_center, prev_center, inertia_center);
                    v3_scale(inertia_center, powf(fmaxf(0.0f, 1.0f-fminf(1.0f,drag)), dt*60.0f), inertia_center);
                    transform_vec(center_world, inertia_center, inertia);
                } else {
                    v3_sub(joint->current_tail, joint->prev_tail, inertia);
                    v3_scale(inertia, powf(fmaxf(0.0f, 1.0f-fminf(1.0f,drag)), dt*60.0f), inertia);
                }
            } else {
                v3_sub(joint->current_tail, joint->prev_tail, inertia);
                v3_scale(inertia, powf(fmaxf(0.0f, 1.0f-fminf(1.0f,drag)), dt*60.0f), inertia);
            }

            /* Scale reference 60 Hz forces for the 120 Hz Verlet step. */
            float stiff_force[3];
            v3_scale(rest_dir, dt * dt * 60.0f * stiffness, stiff_force);

            /* Authored zero gravity is intentional and remains zero. */
            float external[3];
            float grav_dir_use[3] = {grav_dir[0], grav_dir[1], grav_dir[2]};
            v3_scale(grav_dir_use, dt * dt * 60.0f * grav_power, external);

            /* nextTail = currentTail + inertia + stiffness + external */
            float next_tail[3];
            memcpy(next_tail, joint->current_tail, 3*sizeof(float));
            v3_add(next_tail, inertia, next_tail);
            v3_add(next_tail, stiff_force, next_tail);
            v3_add(next_tail, external, next_tail);

            /* Constrain length: nextTail = worldPos + normalize(nextTail - worldPos) * boneLength */
            float to_tail[3];
            v3_sub(next_tail, world_pos, to_tail);
            float len = v3_len(to_tail);
            if (len > 1e-6f) {
                v3_scale(to_tail, joint->bone_length / len, to_tail);
            }
            v3_add(world_pos, to_tail, next_tail);

            float free_tail[3];
            memcpy(free_tail,next_tail,sizeof(free_tail));
            int contacts=solve_contacts(model,group,joint,world_pos,next_tail,cloth_chain,alpha,
                                        cloth_chain ? garment_rest_direction : NULL);
            float velocity[3],normal[3];
            v3_sub(free_tail,joint->current_tail,velocity);
            v3_sub(next_tail,free_tail,normal);
            v3_normalize(normal);
            float inward=v3_dot(velocity,normal);
            if (contacts && inward<0) for (int k=0;k<3;k++) velocity[k]-=inward*normal[k];
            for (int k=0;k<3;k++) joint->prev_tail[k]=next_tail[k]-velocity[k];
            memcpy(joint->current_tail,next_tail,sizeof(joint->current_tail));

            float parent_world[16];
            if (node->parent_index >= 0 && node->parent_index < model->node_count) {
                memcpy(parent_world, model->nodes[node->parent_index].world_matrix, sizeof(parent_world));
            } else {
                float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                memcpy(parent_world, identity, sizeof(parent_world));
            }

            float rest_world[16];
            mat4_mul(parent_world, joint->initial_local_matrix, rest_world);

            float rest_inv[16];
            float new_rot[4];
            if (mat4_inverse(rest_world, rest_inv)) {
                float to_local[3];
                transform_point(rest_inv, next_tail, to_local);
                v3_normalize(to_local);
                float local_delta[4];
                quat_from_to(joint->bone_axis, to_local, local_delta);
                quat_mul(joint->initial_local_rotation, local_delta, new_rot);
            } else {
                memcpy(new_rot, joint->initial_local_rotation, sizeof(new_rot));
            }

            /* Normalize */
            float qlen = sqrtf(new_rot[0]*new_rot[0] + new_rot[1]*new_rot[1] +
                              new_rot[2]*new_rot[2] + new_rot[3]*new_rot[3]);
            if (qlen > 1e-6f) {
                node->rotation[0] = new_rot[0] / qlen;
                node->rotation[1] = new_rot[1] / qlen;
                node->rotation[2] = new_rot[2] / qlen;
                node->rotation[3] = new_rot[3] / qlen;
            }

            update_node_transform(model, joint->node, parent_world);
        }
    }
}

/* Fixed physics clock, independent of rendering rate. Collider motion is
 * interpolated across catch-up steps; long stalls reset velocity instead. */
void spring_bones_update(GltfModel *model, float dt) {
    if (!model || !model->spring_runtime || !isfinite(dt) || dt<=0) return;
    struct SpringRuntime *rt=model->spring_runtime;
    if (dt>0.25f) {
        rt->stats.dropped_time += dt;
        spring_bones_reset(model);
        return;
    }
    capture_colliders(model,0);
    rt->accumulator += dt;
    const double step=1.0/120.0;
    int steps=(int)((rt->accumulator+1e-9)/step);
    if (steps>30) steps=30;
    if (!steps) return;
    rt->stats.contacts=rt->stats.unresolved_contacts=0;
    rt->stats.max_penetration=rt->stats.garment_arm_penetration=0;
    rt->stats.steps=steps;
    for (int i=0;i<steps;i++) spring_step(model,(float)step,(float)(i+1)/steps);
    rt->accumulator -= steps*step;
    capture_colliders(model,1);
}

SpringStats spring_bones_stats(const GltfModel *model) {
    SpringStats zero={0};
    return model && model->spring_runtime ? model->spring_runtime->stats : zero;
}

/* Cleanup */
void spring_bones_cleanup(GltfModel *model) {
    if (!model) return;
    if (model->spring_runtime) {
        free(model->spring_runtime->colliders);
        free(model->spring_runtime->garments);
        free(model->spring_runtime);
        model->spring_runtime=NULL;
    }

    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        free(group->joints);
        free(group->bones);
        free(group->collider_groups);
    }
    free(model->spring_groups);
    model->spring_groups = NULL;
    model->spring_group_count = 0;

    for (int c = 0; c < model->spring_collider_count; c++) {
        free(model->spring_colliders[c].colliders);
    }
    free(model->spring_colliders);
    model->spring_colliders = NULL;
    model->spring_collider_count = 0;
}
