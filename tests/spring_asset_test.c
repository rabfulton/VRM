/* Optional real-model spring stability check; requires an OpenGL display. */
#include "platform.h"
#include "animator.h"
#include "spring_bones.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int garment(const char *name) {
    char lower[256]; size_t i=0;
    for (; name[i] && i<sizeof(lower)-1; i++)
        lower[i]=(name[i]>='A' && name[i]<='Z') ? name[i]+'a'-'A' : name[i];
    lower[i]=0;
    return strstr(lower,"skirt") || strstr(lower,"dress") ||
           strstr(lower,"coat") || strstr(lower,"cloth");
}

static void advance(Animator *animator, GltfModel *model) {
    animator_update(animator,1.0f/120);
    gltf_update_transforms(model);
    spring_bones_update(model,1.0f/120);
    gltf_update_transforms(model);
}

static int valid_joints(const GltfModel *model) {
    for (int g=0;g<model->spring_group_count;g++) {
        const SpringBoneGroup *group=&model->spring_groups[g];
        for (int j=0;j<group->joint_count;j++) {
            const SpringJoint *joint=&group->joints[j]; float length2=0;
            for (int k=0;k<3;k++) {
                float d=joint->current_tail[k]-model->nodes[joint->node].world_matrix[12+k];
                if (!isfinite(d)) return 0;
                length2+=d*d;
            }
            if (fabsf(sqrtf(length2)-joint->bone_length)>0.0001f) return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc<2 || argc>4) {
        fprintf(stderr,"Usage: %s MODEL [ANIMATION [TIME]]\n",argv[0]); return 2;
    }
    PlatformWindow *window=platform_create_window(64,64,"Spring stability",true,false);
    if (!window) return 1;
    platform_hide_window(window);
    GltfModel model={0}; Animator animator;
    if (!gltf_load(&model,argv[1])) return 1;
    gltf_update_transforms(&model);
    spring_bones_init(&model);
    model.anim_correction_mode=0; /* Match the renderer/Studio default. */
    animator_init(&animator); animator_set_use_ozz(&animator,true);
    if (argc>=3 && !gltf_load_animation(&model,argv[2])) return 1;
    animator_set_model(&animator,&model);
    g_spring_gravity=1;
    /* Exercise moving colliders first, then check that a selected fixed pose
     * settles. The production animation/IK and spring update order is shared. */
    if (argc>=3) {
        animator_set_time(&animator,0);
        gltf_update_transforms(&model);
        spring_bones_reset(&model);
        animator_set_loop_policy(&animator,true,true);
        animator_play(&animator,1);
        for (int frame=0;frame<480;frame++) {
            advance(&animator,&model);
            if (!valid_joints(&model)) { fputs("Invalid moving spring joint\n",stderr); return 1; }
        }
        animator_set_time(&animator,argc==4 ? (float)atof(argv[3]) : 0);
    }
    animator_pause(&animator);
    float (*previous)[3]=calloc((size_t)model.node_count,sizeof(*previous));
    if (!previous) return 1;
    double squared[2]={0}; int samples[2]={0}, unresolved=0;
    float peak[2]={0}, penetration=0; const char *worst[2]={"none","none"};
    for (int frame=0;frame<1200;frame++) {
        advance(&animator,&model);
        if (!valid_joints(&model)) { fputs("Invalid settled spring joint\n",stderr); return 1; }
        SpringStats stats=spring_bones_stats(&model);
        if (frame>=960) {
            if (stats.max_penetration>penetration) penetration=stats.max_penetration;
            if (stats.unresolved_contacts>unresolved) unresolved=stats.unresolved_contacts;
        }
        for (int g=0;g<model.spring_group_count;g++) {
            SpringBoneGroup *group=&model.spring_groups[g];
            int category=garment(model.nodes[group->bones[0]].name) ? 0 : 1;
            for (int j=0;j<group->joint_count;j++) {
                SpringJoint *joint=&group->joints[j]; float d2=0;
                for (int k=0;k<3;k++) {
                    float delta=joint->current_tail[k]-previous[joint->node][k];
                    d2+=delta*delta; previous[joint->node][k]=joint->current_tail[k];
                }
                if (frame>=960) {
                    squared[category]+=d2; samples[category]++;
                    if (sqrtf(d2)>peak[category]) {
                        peak[category]=sqrtf(d2); worst[category]=model.nodes[joint->node].name;
                    }
                }
            }
        }
    }
    printf("Settled pose: garment peak %.6f m RMS %.6f m; other peak %.6f m RMS %.6f m; penetration %.6f m, unresolved %d\n",
           peak[0],sqrt(squared[0]/fmax(1,samples[0])),
           peak[1],sqrt(squared[1]/fmax(1,samples[1])),penetration,unresolved);
    printf("Largest tail steps: garment %s; other %s\n",worst[0],worst[1]);
    free(previous);
    animator_cleanup(&animator); gltf_unload(&model);
    platform_destroy_window(window);
    return peak[0]>0.001f ? 1 : 0;
}
