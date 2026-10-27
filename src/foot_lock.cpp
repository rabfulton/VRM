/* Flat-floor foot contacts, independently tracked from the blended pose. */
extern "C" {
#include "foot_lock.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
// glTF and C ABI pose arrays have float alignment, not SIMD alignment.
#define CGLM_ALL_UNALIGNED
#include <cglm/cglm.h>
#ifdef VRM_USE_OZZ
#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/simd_quaternion.h"
#endif

static int humanoid(const GltfModel *model, const char *name) {
    for (int i=0;i<model->humanoid_bone_count;i++)
        if (!strcmp(model->humanoid_names[i],name)) return model->humanoid_bones[i];
    return -1;
}
static bool valid(const GltfModel *model, int node) { return node>=0 && node<model->node_count; }
static bool ancestor(const GltfModel *model, int parent, int child) {
    for (int n=0;valid(model,child) && n<model->node_count;n++,child=model->nodes[child].parent_index)
        if (child==parent) return true;
    return false;
}
static void position(const GltfModel *model,int node,float *out) {
    memcpy(out,model->nodes[node].world_matrix+12,3*sizeof(float));
}
static float distance(const float *a,const float *b) {
    float sum=0;for(int i=0;i<3;i++)sum+=(a[i]-b[i])*(a[i]-b[i]);return sqrtf(sum);
}
static bool world_rotation(const float *matrix,float *rotation) {
    mat4 normalized;
    memcpy(normalized,matrix,sizeof(normalized));
    float scales[3];
    for(int i=0;i<3;i++) {
        scales[i]=glm_vec3_norm(normalized[i]);
        if(scales[i]<1e-7f) return false;
        glm_vec3_scale(normalized[i],1/scales[i],normalized[i]);
    }
    /* Rotations-only IK cannot preserve general sheared/scaled limb geometry. */
    float maximum=std::max({scales[0],scales[1],scales[2]});
    float minimum=std::min({scales[0],scales[1],scales[2]});
    if(maximum>minimum*1.001f || fabsf(glm_vec3_dot(normalized[0],normalized[1]))>0.001f ||
       fabsf(glm_vec3_dot(normalized[0],normalized[2]))>0.001f ||
       fabsf(glm_vec3_dot(normalized[1],normalized[2]))>0.001f || glm_mat4_det(normalized)<=0) return false;
    glm_mat4_quat(normalized,rotation);
    glm_quat_normalize(rotation);
    return true;
}
static bool node_rotation(const GltfModel *model,int node,float *out) {
    return world_rotation(model->nodes[node].world_matrix,out);
}
#ifdef VRM_USE_OZZ
static void set_world_rotation(GltfModel *model,int node,float *world) {
    int parent=model->nodes[node].parent_index;
    versor parent_rotation=GLM_QUAT_IDENTITY_INIT, inverse;
    if(valid(model,parent)) node_rotation(model,parent,parent_rotation);
    glm_quat_inv(parent_rotation,inverse);
    glm_quat_mul(inverse,world,model->nodes[node].rotation);
    glm_quat_normalize(model->nodes[node].rotation);
}
static float contact_weight(float value) { return value*value*(3-2*value); }

/* Derive the hinge and pole from the pose the viewer sees, never from the
 * unblended incoming clip. Near a straight knee retain the previous plane. */
static void knee_plane(FootPlant &foot,const GltfModel *model) {
    float hip[3],knee[3],ankle[3],ab[3],bc[3],axis[3],rotation[4];
    position(model,foot.hip,hip);position(model,foot.knee,knee);position(model,foot.ankle,ankle);
    glm_vec3_sub(knee,hip,ab);glm_vec3_sub(ankle,knee,bc);
    glm_vec3_cross(ab,bc,axis);
    if(glm_vec3_norm(axis)>foot.length*foot.length*0.002f) {
        float direction[3],bend[3];glm_vec3_sub(ankle,hip,direction);glm_vec3_normalize(direction);
        float along=glm_vec3_dot(ab,direction);
        for(int k=0;k<3;k++)bend[k]=ab[k]-along*direction[k];
        glm_vec3_normalize_to(bend,foot.pole);
        glm_vec3_negate(axis);glm_vec3_normalize(axis);
        if(node_rotation(model,foot.knee,rotation)) {
            versor inverse;glm_quat_inv(rotation,inverse);glm_quat_rotatev(inverse,axis,foot.knee_axis);
            glm_vec3_normalize(foot.knee_axis);
        }
    }
}

static void smooth_pelvis(FootLock *lock,float target,float dt) {
    /* Exact critically damped response: no frame-size-dependent lerp, no snap
     * when the limiting foot changes or the final contact releases. */
    constexpr float omega=45.0f;
    float displacement=lock->pelvis_offset-target;
    float velocity_term=lock->pelvis_velocity+omega*displacement;
    float decay=expf(-omega*dt);
    lock->pelvis_offset=target+(displacement+velocity_term*dt)*decay;
    lock->pelvis_velocity=(lock->pelvis_velocity-omega*velocity_term*dt)*decay;
}
#endif

void foot_lock_init(FootLock *lock) {
    memset(lock,0,sizeof(*lock));lock->enabled=true;lock->clip_allowed=true;lock->calibrate_floor=true;lock->pelvis=-1;
    for(auto &foot:lock->feet) foot.hip=foot.knee=foot.ankle=foot.toes=-1;
}
void foot_lock_reset(FootLock *lock) {
    lock->pelvis_offset=lock->pelvis_velocity=0;
    for(auto &foot:lock->feet) {
        foot.planted=foot.has_reference=foot.wants_contact=false;
        foot.stable_time=foot.weight=foot.error=foot.speed=0;
    }
}
void foot_lock_set_model(FootLock *lock,GltfModel *model) {
    if(model && lock->nodes==model->nodes && lock->node_count==model->node_count) return;
    bool enabled=lock->enabled;
    foot_lock_init(lock);lock->enabled=enabled;
    if(!model) return;
    lock->nodes=model->nodes;lock->node_count=model->node_count;
    lock->pelvis=humanoid(model,"hips");
    const char *names[2][4]={{"leftUpperLeg","leftLowerLeg","leftFoot","leftToes"},
                           {"rightUpperLeg","rightLowerLeg","rightFoot","rightToes"}};
    for(int i=0;i<2;i++) {
        auto &foot=lock->feet[i];
        foot.hip=humanoid(model,names[i][0]);foot.knee=humanoid(model,names[i][1]);
        foot.ankle=humanoid(model,names[i][2]);foot.toes=humanoid(model,names[i][3]);
        if(!valid(model,foot.hip)||!valid(model,foot.knee)||!valid(model,foot.ankle)||
           foot.hip==foot.knee || foot.knee==foot.ankle ||
           model->nodes[foot.hip].is_spring_bone || model->nodes[foot.knee].is_spring_bone ||
           model->nodes[foot.ankle].is_spring_bone || !ancestor(model,foot.hip,foot.knee)||!ancestor(model,foot.knee,foot.ankle)) continue;
        float hip[3],knee[3],ankle[3],rotation[4];
        position(model,foot.hip,hip);position(model,foot.knee,knee);position(model,foot.ankle,ankle);
        foot.length=distance(hip,knee)+distance(knee,ankle);foot.ground_height=ankle[1];
#ifdef VRM_USE_OZZ
        foot.supported=distance(hip,knee)>1e-5f && distance(knee,ankle)>1e-5f && node_rotation(model,foot.hip,rotation) &&
                       node_rotation(model,foot.knee,rotation) && node_rotation(model,foot.ankle,rotation);
#else
        (void)rotation;
#endif
        foot.pole[2]=1;foot.knee_axis[0]=-1;
#ifdef VRM_USE_OZZ
        if(foot.supported) knee_plane(foot,model);
#endif
    }
}
void foot_lock_reset_reference(FootLock *lock) {
    for(auto &foot:lock->feet) { foot.has_reference=false;foot.stable_time=0; }
}
void foot_lock_new_clip(FootLock *lock) {
    foot_lock_reset_reference(lock);
    lock->calibrate_floor=true;
}

void foot_lock_observe(FootLock *lock,GltfModel *model,float dt) {
    if(!lock->enabled || !lock->clip_allowed || !model || model->nodes!=lock->nodes) return;
    if(!std::isfinite(dt)||dt<=0 || dt>0.1f) { foot_lock_reset(lock); return; }
    if(lock->calibrate_floor) {
        float lowest=INFINITY, length=0;
        for(const auto &foot:lock->feet) if(foot.supported) {
            float ankle[3];position(model,foot.ankle,ankle);
            lowest=std::min(lowest,ankle[1]-foot.ground_height);
            length=std::max(length,foot.length);
        }
        /* Calibrate a small constant retargeting height mismatch, never a jump
         * far above the bind floor. The physical ankle plane remains fixed. */
        lock->floor_offset=lowest>=-length*0.1f && lowest<=length*0.18f ? lowest : 0;
        lock->calibrate_floor=false;
    }
    for(auto &foot:lock->feet) {
        if(!foot.supported) continue;
        float ankle[3],rotation[4];
        position(model,foot.ankle,ankle);
        if(!node_rotation(model,foot.ankle,rotation)) { foot.planted=false;foot.wants_contact=false;continue; }
        foot.speed=foot.has_reference ? distance(ankle,foot.reference)/dt : 0;
        float height=ankle[1]-(foot.ground_height+lock->floor_offset);
        float tolerance=foot.length*0.035f;
        float speed_limit=foot.length*(foot.planted ? 0.22f : 0.08f);
        bool grounded=height<=tolerance && height>=-foot.length*0.12f;
        bool contact=grounded && foot.speed<speed_limit;
        if(foot.planted) {
            float rotation_dot=0;for(int k=0;k<4;k++) rotation_dot+=rotation[k]*foot.anchor_rotation[k];
            if(distance(ankle,foot.anchor)>foot.length*0.2f || fabsf(rotation_dot)<0.9238795f) contact=false;
        }
        foot.stable_time=contact ? foot.stable_time+dt : 0;
        foot.wants_contact=contact;
        if(!contact) foot.planted=false;
        memcpy(foot.reference,ankle,sizeof(ankle));memcpy(foot.reference_rotation,rotation,sizeof(rotation));
        foot.has_reference=true;
    }
}

void foot_lock_apply(FootLock *lock,GltfModel *model,float dt) {
    if(!lock->enabled || !lock->clip_allowed || !model || model->nodes!=lock->nodes || dt<=0 || dt>0.1f) return;
#ifdef VRM_USE_OZZ
    float targets[2][3]{};
    float pelvis_drop=0,maximum_drop=0;
    for(int side=0;side<2;side++) {
        auto &foot=lock->feet[side];
        if(!foot.supported) continue;
        float rotation[4];
        if(!node_rotation(model,foot.hip,rotation)||!node_rotation(model,foot.knee,rotation)||!node_rotation(model,foot.ankle,rotation)) {
            foot.planted=false;foot.weight=0;foot.error=0;continue;
        }
        float ankle[3],hip[3];position(model,foot.ankle,ankle);position(model,foot.hip,hip);
        if(!foot.planted && foot.wants_contact && foot.stable_time>=0.06f && foot.weight<=0) {
            foot.planted=true;memcpy(foot.anchor,ankle,sizeof(ankle));
            foot.anchor[1]=foot.ground_height;
            node_rotation(model,foot.ankle,foot.anchor_rotation);
        }
        if(foot.planted && distance(hip,foot.anchor)>foot.length*1.2f) foot.planted=false;
        foot.weight=foot.planted ? std::min(1.0f,foot.weight+dt/0.08f) : std::max(0.0f,foot.weight-dt/0.12f);
        float weight=contact_weight(foot.weight);
        memcpy(targets[side],foot.anchor,sizeof(foot.anchor));
        if(weight<=0) { foot.error=0;continue; }
        float dx=hip[0]-targets[side][0],dz=hip[2]-targets[side][2];
        float reach=foot.length*0.995f;
        float vertical2=reach*reach-dx*dx-dz*dz;
        if(vertical2>0 && hip[1]>targets[side][1]) {
            float allowed=targets[side][1]+sqrtf(vertical2);
            pelvis_drop=std::min(pelvis_drop,(allowed-hip[1])*weight);
            maximum_drop=std::max(maximum_drop,foot.length*0.18f*weight);
        }
    }
    smooth_pelvis(lock,std::max(pelvis_drop,-maximum_drop),dt);
    if(valid(model,lock->pelvis) && fabsf(lock->pelvis_offset)>1e-7f) {
        int parent=model->nodes[lock->pelvis].parent_index;
        vec3 local_delta={0,lock->pelvis_offset,0};
        if(valid(model,parent)) {
            mat4 inverse;glm_mat4_inv((vec4*)model->nodes[parent].world_matrix,inverse);
            vec3 world_delta={0,lock->pelvis_offset,0};glm_mat4_mulv3(inverse,world_delta,0,local_delta);
        }
        for(int k=0;k<3;k++)model->nodes[lock->pelvis].translation[k]+=local_delta[k];
        gltf_update_transforms(model);
    }
    for(int side=0;side<2;side++) {
        auto &foot=lock->feet[side];
        if(!foot.supported || foot.weight<=0) continue;
        float rotation[4];
        if(!node_rotation(model,foot.hip,rotation)||!node_rotation(model,foot.knee,rotation)||!node_rotation(model,foot.ankle,rotation)) {
            foot.planted=false;foot.weight=0;continue;
        }
        knee_plane(foot,model);
        ozz::math::Float4x4 matrices[3];int nodes[]={foot.hip,foot.knee,foot.ankle};
        for(int n=0;n<3;n++) for(int col=0;col<4;col++)
            matrices[n].cols[col]=ozz::math::simd_float4::LoadPtrU(model->nodes[nodes[n]].world_matrix+col*4);
        ozz::math::SimdQuaternion hip_correction,knee_correction;
        ozz::animation::IKTwoBoneJob job;
        job.start_joint=&matrices[0];job.mid_joint=&matrices[1];job.end_joint=&matrices[2];
        job.target=ozz::math::simd_float4::Load3PtrU(targets[side]);
        job.pole_vector=ozz::math::simd_float4::Load3PtrU(foot.pole);
        job.mid_axis=ozz::math::simd_float4::Load3PtrU(foot.knee_axis);
        job.start_joint_correction=&hip_correction;job.mid_joint_correction=&knee_correction;
        /* Weight rotations as well as position: blending only the target still
         * applies a full knee-plane correction at arbitrarily small influence. */
        job.soften=1;job.weight=contact_weight(foot.weight);
        if(!job.Run()) { foot.planted=false;continue; }
        versor correction,result;
        ozz::math::StorePtrU(hip_correction.xyzw,correction);
        glm_quat_mul(model->nodes[foot.hip].rotation,correction,result);
        glm_quat_normalize_to(result,model->nodes[foot.hip].rotation);
        ozz::math::StorePtrU(knee_correction.xyzw,correction);
        glm_quat_mul(model->nodes[foot.knee].rotation,correction,result);
        glm_quat_normalize_to(result,model->nodes[foot.knee].rotation);
        gltf_update_transforms(model);
        node_rotation(model,foot.ankle,rotation);
        glm_quat_slerp(rotation,foot.anchor_rotation,contact_weight(foot.weight),result);
        set_world_rotation(model,foot.ankle,result);
        gltf_update_transforms(model);
        float ankle[3];position(model,foot.ankle,ankle);foot.error=distance(ankle,foot.anchor);
        if(foot.weight>=0.99f && foot.error>foot.length*0.04f) foot.planted=false;
    }
#else
    (void)dt;
#endif
}
