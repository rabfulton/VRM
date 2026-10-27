extern "C" {
#include "foot_lock.h"
}
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct Rig {
    GltfModel model{};
    GltfNode nodes[10]{};
    int children[10][3]{};
    int root=0;
    char names[9][32]={"hips","leftUpperLeg","leftLowerLeg","leftFoot","leftToes",
                       "rightUpperLeg","rightLowerLeg","rightFoot","rightToes"};
    int humanoid[9]={1,2,3,4,5,6,7,8,9};
    Rig() {
        int parents[]={-1,0,1,2,3,4,1,6,7,8};
        for(int i=0;i<10;i++) {
            auto &n=nodes[i];n.parent_index=parents[i];n.children=children[i];
            n.rotation[3]=n.base_rotation[3]=1;
            for(int k=0;k<3;k++)n.scale[k]=n.base_scale[k]=1;
            if(parents[i]>=0)nodes[parents[i]].children[nodes[parents[i]].child_count++]=i;
        }
        nodes[1].base_translation[1]=1.02f;
        nodes[2].base_translation[0]=-0.1f;nodes[6].base_translation[0]=0.1f;
        for(int n:{3,7}) { nodes[n].base_translation[1]=-0.45f;nodes[n].base_translation[2]=0.02f; }
        for(int n:{4,8}) { nodes[n].base_translation[1]=-0.45f;nodes[n].base_translation[2]=-0.02f; }
        for(int n:{5,9}) { nodes[n].base_translation[1]=-0.05f;nodes[n].base_translation[2]=0.15f; }
        model.nodes=nodes;model.node_count=10;model.scene_roots=&root;model.scene_root_count=1;
        model.humanoid_names=names;model.humanoid_bones=humanoid;model.humanoid_bone_count=9;
        raw();
    }
    void raw(float offset=0,float lift=0,float left_angle=0) {
        for(auto &n:nodes) {
            memcpy(n.translation,n.base_translation,sizeof(n.translation));
            memcpy(n.rotation,n.base_rotation,sizeof(n.rotation));
        }
        nodes[1].translation[0]+=offset;nodes[1].translation[1]+=lift;
        nodes[2].rotation[0]=sinf(left_angle/2);nodes[2].rotation[3]=cosf(left_angle/2);
        gltf_update_transforms(&model);
    }
    void step(FootLock &lock,float dt,float offset=0,float lift=0,float left_angle=0) {
        raw(offset,lift,left_angle);foot_lock_observe(&lock,&model,dt);foot_lock_apply(&lock,&model,dt);
    }
};
static void near(float a,float b,float tolerance=0.001f) {
    if(fabsf(a-b)>tolerance) { std::fprintf(stderr,"%g != %g (tolerance %g)\n",a,b,tolerance);assert(false); }
}
static void idle(Rig &rig,FootLock &lock,int fps=60) {
    foot_lock_reset(&lock);
    for(int i=0;i<fps/2;i++)rig.step(lock,1.0f/fps);
    assert(lock.feet[0].planted && lock.feet[1].planted);
    near(lock.feet[0].weight,1);near(lock.feet[1].weight,1);
}
static float segment_length(Rig &rig,int a,int b) {
    float length=0;for(int k=0;k<3;k++) { float d=rig.nodes[a].world_matrix[12+k]-rig.nodes[b].world_matrix[12+k];length+=d*d; }
    return sqrtf(length);
}
static void continuity_regressions() {
    for(int fps:{30,60,144}) {
        Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
        idle(rig,lock,fps);
        float knee[3];memcpy(knee,rig.nodes[3].world_matrix+12,sizeof(knee));
        /* A new target can have an opposite knee plane while the displayed
         * blend still shows the outgoing pose. It must not flip the knee. */
        foot_lock_new_clip(&lock);
        rig.raw();rig.nodes[3].translation[2]=-0.02f;rig.nodes[4].translation[2]=0.02f;
        gltf_update_transforms(&rig.model);foot_lock_observe(&lock,&rig.model,1.0f/fps);
        rig.raw();foot_lock_apply(&lock,&rig.model,1.0f/fps);
        for(int k=0;k<3;k++)near(rig.nodes[3].world_matrix[12+k],knee[k],0.002f);
    }
    /* Pelvis adjustment must approach zero frame motion as dt approaches zero,
     * including when an incoming clip changes the required reach abruptly. */
    Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
    idle(rig,lock);
    const float before=lock.pelvis_offset;
    foot_lock_new_clip(&lock);rig.raw(0,0.08f);
    foot_lock_observe(&lock,&rig.model,0.0001f);foot_lock_apply(&lock,&rig.model,0.0001f);
    near(lock.pelvis_offset,before,0.0001f);
}
int main() {
    continuity_regressions();
    for(int fps:{30,60,144}) {
        Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
        assert(lock.feet[0].supported && lock.feet[1].supported);
        idle(rig,lock,fps);
        for(auto &foot:lock.feet) {
            assert(foot.error<0.001f);
            for(int k=0;k<3;k++)near(rig.nodes[foot.ankle].world_matrix[12+k],foot.anchor[k]);
        }
        foot_lock_new_clip(&lock);
        for(int i=0;i<fps/2;i++) {
            rig.raw(0.08f);foot_lock_observe(&lock,&rig.model,1.0f/fps);
            rig.raw(0.08f*(i+1)/(fps/2));foot_lock_apply(&lock,&rig.model,1.0f/fps);
            for(auto &foot:lock.feet) {
                if(foot.error>=0.001f) std::fprintf(stderr,"fps=%d frame=%d side=%d error=%g drop=%g\n",fps,i,foot.ankle,foot.error,lock.pelvis_offset);
                assert(foot.planted && foot.error<0.001f);
                near(segment_length(rig,foot.hip,foot.knee),sqrtf(0.45f*0.45f+0.02f*0.02f));
                near(segment_length(rig,foot.knee,foot.ankle),sqrtf(0.45f*0.45f+0.02f*0.02f));
                near(rig.nodes[foot.ankle].world_matrix[0],1,0.001f);
                near(rig.nodes[foot.ankle].world_matrix[5],1,0.001f);
            }
        }
        /* Lift one leg: release that foot smoothly while the other stays planted. */
        rig.step(lock,1.0f/fps,0.08f,0,0.8f);
        assert(!lock.feet[0].planted && lock.feet[0].weight>0 && lock.feet[1].planted);
        for(int i=0;i<fps/4;i++)rig.step(lock,1.0f/fps,0.08f,0,0.8f);
        near(lock.feet[0].weight,0);assert(lock.feet[1].planted);
        /* A jump is free to lift both feet. */
        idle(rig,lock,fps);foot_lock_new_clip(&lock);
        for(int i=0;i<fps/4;i++)rig.step(lock,1.0f/fps,0,0.3f);
        for(auto &foot:lock.feet) { assert(!foot.planted);near(foot.weight,0); }
        near(rig.nodes[1].translation[1],1.32f);
        /* A walking step is detected from target motion even at ground height. */
        idle(rig,lock,fps);
        for(int i=0;i<fps/3;i++)rig.step(lock,1.0f/fps,0.5f*(i+1)/fps);
        for(auto &foot:lock.feet) { assert(!foot.planted);near(foot.weight,0); }
        /* Teleports are bounded and cannot stretch either bone. */
        idle(rig,lock,fps);rig.step(lock,1.0f/fps,3);
        for(auto &foot:lock.feet) {
            assert(!foot.planted && std::isfinite(foot.error));
            near(segment_length(rig,foot.hip,foot.knee),sqrtf(0.45f*0.45f+0.02f*0.02f));
        }
        foot_lock_reset(&lock);for(auto &foot:lock.feet)near(foot.weight,0);
    }
    /* A constant clip-entry retargeting offset is calibrated and grounded. */
    {
        Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
        for(int i=0;i<30;i++)rig.step(lock,1.0f/60,0,0.08f);
        near(lock.floor_offset,0.08f);
        assert(lock.feet[0].planted && lock.feet[1].planted);
        assert(lock.feet[0].error<0.001f && lock.feet[1].error<0.001f);
        /* Resetting reference/seek history in mid-jump cannot make a new floor. */
        foot_lock_reset(&lock);
        for(int i=0;i<15;i++)rig.step(lock,1.0f/60,0,0.3f);
        near(lock.floor_offset,0.08f);
        assert(!lock.feet[0].planted && !lock.feet[1].planted);
    }
    /* A clip starting well above the floor must not turn into a planted pose. */
    {
        Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
        for(int i=0;i<30;i++)rig.step(lock,1.0f/60,0,0.4f);
        near(lock.floor_offset,0);
        assert(!lock.feet[0].planted && !lock.feet[1].planted);
        idle(rig,lock);
        foot_lock_new_clip(&lock);
        rig.raw();rig.nodes[1].rotation[1]=sinf(0.75f);rig.nodes[1].rotation[3]=cosf(0.75f);
        gltf_update_transforms(&rig.model);
        foot_lock_observe(&lock,&rig.model,1.0f/60);foot_lock_apply(&lock,&rig.model,1.0f/60);
        assert(!lock.feet[0].planted && !lock.feet[1].planted);
    }
    /* Clip contact policy leaves user preference intact and releases the pose. */
    {
        Rig rig;FootLock lock;foot_lock_init(&lock);foot_lock_set_model(&lock,&rig.model);
        lock.clip_allowed=false;
        for(int i=0;i<60;i++)rig.step(lock,1.0f/60);
        assert(lock.enabled && !lock.feet[0].planted && !lock.feet[1].planted);
        near(lock.pelvis_offset,0);
        lock.clip_allowed=true;idle(rig,lock);
        assert(lock.feet[0].planted && lock.feet[1].planted);
    }
    /* Missing mappings and nonuniform limb scale gracefully preserve animation. */
    Rig rig;FootLock lock;foot_lock_init(&lock);rig.model.humanoid_bone_count=0;
    foot_lock_set_model(&lock,&rig.model);assert(!lock.feet[0].supported);rig.step(lock,1.0f/60);
    rig.model.humanoid_bone_count=9;foot_lock_set_model(&lock,nullptr);
    rig.nodes[2].scale[0]=2;gltf_update_transforms(&rig.model);
    foot_lock_set_model(&lock,&rig.model);assert(!lock.feet[0].supported);
    std::puts("Foot planting tests passed: stable anchors/orientation, blend/root motion, 30/60/144 Hz, lift/step/jump release, reach limits and unsupported rigs");
}
