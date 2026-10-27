/* Real ozz sampling + Animator orchestration with a synthetic rig; no GL context. */
extern "C" {
#include "animator.h"
}
#include "ozz_bridge.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static void foot_transition_continuity(int fps) {
    GltfModel model{};GltfNode nodes[7]{};GltfSkin skin{};
    int children[7][2]{},parents[]={-1,0,1,2,0,4,5},joints[]={0,1,2,3,4,5,6},root=0;
    char names[7][32]={"hips","leftUpperLeg","leftLowerLeg","leftFoot",
                       "rightUpperLeg","rightLowerLeg","rightFoot"};
    for(int i=0;i<7;i++) {
        auto &node=nodes[i];node.parent_index=parents[i];node.children=children[i];
        node.rotation[3]=node.base_rotation[3]=1;
        for(int k=0;k<3;k++)node.scale[k]=node.base_scale[k]=1;
        if(parents[i]>=0)nodes[parents[i]].children[nodes[parents[i]].child_count++]=i;
    }
    nodes[0].translation[1]=nodes[0].base_translation[1]=1.02f;
    nodes[1].translation[0]=nodes[1].base_translation[0]=-0.1f;
    nodes[4].translation[0]=nodes[4].base_translation[0]=0.1f;
    for(int i:{2,3,5,6})nodes[i].translation[1]=nodes[i].base_translation[1]=-0.45f;
    for(int i:{2,5})nodes[i].translation[2]=nodes[i].base_translation[2]=0.02f;
    for(int i:{3,6})nodes[i].translation[2]=nodes[i].base_translation[2]=-0.02f;
    skin.joints=joints;skin.joint_count=7;
    model.loaded=true;model.nodes=nodes;model.node_count=7;model.skins=&skin;model.skin_count=1;
    model.scene_roots=&root;model.scene_root_count=1;model.humanoid_names=names;
    model.humanoid_bones=joints;model.humanoid_bone_count=7;model.anim_global_correction[3]=1;
    float times[]={0,4},translations[]={0,1.10f,0,0,1.10f,0};uint8_t masks[]={1,0,0,0,0,0,0};
    GltfAnimSampler sampler{};sampler.input=times;sampler.output=translations;sampler.keyframe_count=2;sampler.output_stride=3;
    GltfAnimChannel channel{};channel.target_node=0;channel.path=GLTF_PATH_TRANSLATION;
    GltfAnimation clip{};clip.duration=4;clip.samplers=&sampler;clip.sampler_count=1;
    clip.channels=&channel;clip.channel_count=1;clip.node_mask=masks;clip.node_mask_count=7;
    model.animations=&clip;model.animation_count=1;gltf_update_transforms(&model);
    Animator anim;animator_init(&anim);animator_set_model(&anim,&model);animator_set_use_ozz(&anim,true);
    animator_set_animation(&anim,0);animator_play(&anim,1);
    for(int i=0;i<fps*2;i++)animator_update(&anim,1.0f/fps);
    assert(anim.foot_lock.feet[0].planted && anim.foot_lock.pelvis_offset < -0.07f);
    const float before=nodes[0].translation[1];
    animator_blend_to(&anim,0,0.45f,-1);animator_update(&anim,0.0001f);
    if(std::fabs(nodes[0].translation[1]-before)>=0.0001f)
        std::fprintf(stderr,"Planted transition pelvis pop: %g -> %g\n",before,nodes[0].translation[1]);
    assert(std::fabs(nodes[0].translation[1]-before)<0.0001f);
    for(int i=0;i<fps;i++) {
        animator_update(&anim,1.0f/fps);
        assert(std::fabs(nodes[0].translation[1]-before)<0.0002f);
        assert(anim.foot_lock.feet[0].error<0.001f);
    }
    /* Pausing must clear velocity without replacing the upstream pose history
     * with the already corrected model pose. Repeated interruptions too. */
    for(int i=0;i<3;i++) {
        animator_pause(&anim);animator_blend_to(&anim,0,0.45f,-1);animator_play(&anim,1);
        animator_update(&anim,0.0001f);
        assert(std::fabs(nodes[0].translation[1]-before)<0.0002f);
    }
    /* Animation-file reloads rebuild ozz, but retain pre-IK snapshots/anchors. */
    ozz_bridge_snapshot_pose(&model,0,anim.time);
    animator_set_model(&anim,&model);animator_pause(&anim);
    assert(ozz_bridge_snapshot_available());
    animator_blend_to(&anim,0,0.45f,-1);animator_play(&anim,1);
    animator_update(&anim,0.0001f);
    assert(std::fabs(nodes[0].translation[1]-before)<0.0002f);
    /* Metadata prevents forced repeats without replacing seam compatibility. */
    animator_set_loop_policy(&anim,true,false);
    assert(animator_compute_loop_remaining(&anim,0)==0);
    animator_blend_to(&anim,0,0.2f,-1);
    assert(anim.loop_remaining==0);
    animator_set_animation(&anim,0);
    assert(anim.loop_remaining==0);
    animator_set_loop_policy(&anim,true,true);
    assert(animator_compute_loop_remaining(&anim,0)==-1);
    animator_blend_to(&anim,0,0.2f,0);
    assert(anim.loop_remaining==0);
    animator_cleanup(&anim);ozz_bridge_snapshot_invalidate();
}

int main() {
    for(int fps:{30,60,144})foot_transition_continuity(fps);
    GltfModel model{};GltfNode nodes[2]{};int child=1,root=0,joints[]={0,1};GltfSkin skin{};
    char names[2][32]={"hips","head"};int humanoid[]={0,1};
    for(int i=0;i<2;i++) {
        nodes[i].rotation[3]=nodes[i].base_rotation[3]=1;
        for(int k=0;k<3;k++)nodes[i].scale[k]=nodes[i].base_scale[k]=1;
        std::snprintf(nodes[i].name,sizeof(nodes[i].name),"node%d",i);
    }
    nodes[0].parent_index=-1;nodes[0].children=&child;nodes[0].child_count=1;nodes[1].parent_index=0;
    skin.joints=joints;skin.joint_count=2;
    model.loaded=true;model.nodes=nodes;model.node_count=2;model.skins=&skin;model.skin_count=1;
    model.scene_roots=&root;model.scene_root_count=1;model.humanoid_names=names;model.humanoid_bones=humanoid;model.humanoid_bone_count=2;
    model.anim_global_correction[3]=1;
    float times[]={0,4};float from[]={0,0,0,4,0,0},to[]={10,0,0,14,0,0};
    float head_a[]={0,0.5f,0,0.8660254f,0,0.5f,0,0.8660254f};
    float head_b[]={0,-0.5f,0,0.8660254f,0,-0.5f,0,0.8660254f};
    GltfAnimSampler samplers[2][2]{};GltfAnimChannel channels[2][2]{};GltfAnimation clips[2]{};
    uint8_t masks[]={1,2};
    for(int clip=0;clip<2;clip++) {
        samplers[clip][0].input=times;samplers[clip][0].output=clip ? to : from;
        samplers[clip][0].keyframe_count=2;samplers[clip][0].output_stride=3;
        samplers[clip][1].input=times;samplers[clip][1].output=clip ? head_b : head_a;
        samplers[clip][1].keyframe_count=2;samplers[clip][1].output_stride=4;
        channels[clip][0].target_node=0;channels[clip][0].sampler_index=0;channels[clip][0].path=GLTF_PATH_TRANSLATION;
        channels[clip][1].target_node=1;channels[clip][1].sampler_index=1;channels[clip][1].path=GLTF_PATH_ROTATION;
        clips[clip].duration=4;clips[clip].samplers=samplers[clip];clips[clip].sampler_count=2;
        clips[clip].channels=channels[clip];clips[clip].channel_count=2;clips[clip].node_mask=masks;clips[clip].node_mask_count=2;
    }
    model.animations=clips;model.animation_count=2;
    for(float &factor:g_anim_blend_group_factors)factor=1;
    g_anim_blend_group_factors[BLEND_GROUP_HEAD]=0.5f;
    Animator anim;animator_init(&anim);animator_set_model(&anim,&model);animator_set_use_ozz(&anim,true);
    animator_set_animation(&anim,0);animator_play(&anim,1);
    for(int i=0;i<30;i++)animator_update(&anim,0.01f);
    float source=nodes[0].translation[0];
    ozz_bridge_snapshot_pose(&model,0,anim.time);
    /* The old clip no longer exists, as happens on load_animation from another file. */
    model.animations=&clips[1];model.animation_count=1;
    animator_set_model(&anim,&model);animator_pause(&anim);
    assert(ozz_bridge_snapshot_available());
    animator_blend_to(&anim,0,0.5f,-1);animator_play(&anim,1);
    assert(anim.time==0 && ozz_bridge_blend_active());
    animator_update(&anim,0.0001f);
    assert(std::fabs((nodes[0].translation[0]-source)/0.0001f-1)<0.02f);
    for(int i=0;i<49;i++)animator_update(&anim,0.01f);
    animator_update(&anim,0.0099f);
    assert(!ozz_bridge_blend_active());
    assert(std::fabs(nodes[0].translation[0]-(10+anim.time))<0.002f);
    float end=nodes[0].translation[0];animator_update(&anim,0.01f);
    assert(std::fabs(nodes[0].translation[0]-end-0.01f)<0.002f);
    /* Restart/interruption preserves displayed pose rather than re-sampling an old source. */
    source=nodes[0].translation[0];animator_blend_to(&anim,0,0.5f,-1);
    assert(std::fabs(nodes[0].translation[0]-source)<1e-6f);
    animator_update(&anim,0.0001f);
    assert(std::fabs((nodes[0].translation[0]-source)/0.0001f-1)<0.03f);
    /* Transition timing uses real time even at half playback speed. */
    animator_play(&anim,0.5f);
    for(int i=0;i<51;i++)animator_update(&anim,0.01f);
    assert(!ozz_bridge_blend_active());
    source=nodes[0].translation[0];animator_blend_to(&anim,0,0.5f,-1);
    assert(ozz_bridge_blend_active());animator_set_time(&anim,0.3f);
    assert(!ozz_bridge_blend_active());
    assert(std::fabs(nodes[0].translation[0]-10.3f)<0.002f);
    /* Low/zero group values never introduce the rest pose during a transition. */
    g_anim_blend_group_factors[BLEND_GROUP_HEAD]=0;
    animator_blend_to(&anim,0,0.5f,-1);animator_update(&anim,0.01f);
    assert(std::fabs(nodes[1].rotation[1]+0.5f)<0.002f);
    ozz_bridge_blend_start(0,anim.time,0,0,0.5f,1);
    ozz_bridge_blend_tick(&model,0.6f,0.1f);
    assert(std::fabs(nodes[0].translation[0]-10.1f)<0.002f);
    animator_cleanup(&anim);ozz_bridge_snapshot_invalidate();
    std::puts("Ozz transition tests passed: reload, source velocity, endpoint, restart, speed, seek, groups and planted pelvis continuity at 30/60/144 Hz");
}
