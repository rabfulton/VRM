#include "spring_bones.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void node(GltfNode *n,float x,float y,float z) {
    n->parent_index=-1;
    n->rotation[3]=n->base_rotation[3]=1;
    n->scale[0]=n->scale[1]=n->scale[2]=1;
    n->base_scale[0]=n->base_scale[1]=n->base_scale[2]=1;
    n->world_matrix[0]=n->world_matrix[5]=n->world_matrix[10]=n->world_matrix[15]=1;
    n->translation[0]=n->base_translation[0]=n->world_matrix[12]=x;
    n->translation[1]=n->base_translation[1]=n->world_matrix[13]=y;
    n->translation[2]=n->base_translation[2]=n->world_matrix[14]=z;
}
static GltfModel fixture(int garment,int assigned,int capsule) {
    GltfModel m={0};m.node_count=3;m.nodes=calloc(3,sizeof(*m.nodes));assert(m.nodes);
    node(&m.nodes[0],0,0,0);node(&m.nodes[1],0,-1,0);node(&m.nodes[2],0,-0.5f,0);
    snprintf(m.nodes[0].name,sizeof(m.nodes[0].name),"%s",garment ? "SkirtFront" : "Hair");
    m.nodes[0].child_count=1;m.nodes[0].children=malloc(sizeof(int));m.nodes[0].children[0]=1;
    m.nodes[1].parent_index=0;
    m.spring_group_count=1;m.spring_groups=calloc(1,sizeof(*m.spring_groups));
    SpringBoneGroup *g=m.spring_groups;g->center=-1;g->bone_count=2;g->bones=malloc(2*sizeof(int));g->bones[0]=0;g->bones[1]=1;
    g->joint_count=1;g->joints=calloc(1,sizeof(*g->joints));g->joints[0].drag_force=0.4f;g->joints[0].hit_radius=0.02f;
    if (assigned) { g->collider_group_count=1;g->collider_groups=malloc(sizeof(int));g->collider_groups[0]=0; }
    m.spring_collider_count=1;m.spring_colliders=calloc(1,sizeof(*m.spring_colliders));
    m.spring_colliders[0].collider_count=1;m.spring_colliders[0].colliders=calloc(1,sizeof(SpringCollider));
    SpringCollider *c=m.spring_colliders[0].colliders;c->node=2;c->radius=0.1f;c->is_capsule=capsule;
    if(capsule) { c->offset[2]=-0.2f;c->tail[2]=0.2f; }
    m.humanoid_bone_count=1;m.humanoid_names=calloc(1,sizeof(*m.humanoid_names));strcpy(m.humanoid_names[0],"leftLowerArm");
    m.humanoid_bones=malloc(sizeof(int));m.humanoid_bones[0]=2;
    return m;
}
static void cleanup(GltfModel *m) {
    spring_bones_cleanup(m);
    for(int i=0;i<m->node_count;i++)free(m->nodes[i].children);
    free(m->nodes);free(m->humanoid_names);free(m->humanoid_bones);
}
static float length(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static void simulate(GltfModel *m,int fps,int seconds) {
    for(int i=0;i<fps*seconds;i++)spring_bones_update(m,1.0f/fps);
}
int main(void) {
    g_spring_gravity=1;g_spring_drag=1;g_spring_stiffness=1;g_spring_cloth_mode=0;
    /* A body volume touching the middle of a skirt bone must be considered,
       even when both endpoints are outside it and the file omitted arm groups. */
    for(int capsule=0;capsule<2;capsule++) {
        GltfModel m=fixture(1,0,capsule);spring_bones_init(&m);
        assert(spring_bones_stats(&m).garment_chains==1);
        assert(spring_bones_stats(&m).arm_colliders==1);
        simulate(&m,60,1);
        assert(spring_bones_stats(&m).max_penetration<0.0001f);
        float *tail=m.spring_groups[0].joints[0].current_tail;
        assert(fabsf(length(tail)-1)<0.00001f);
        for(int k=0;k<3;k++)assert(fabsf(tail[k]-m.nodes[1].world_matrix[12+k])<1e-5f);
        assert(fabsf(tail[0])>0.1f || fabsf(tail[2])>0.1f);
        cleanup(&m);
    }
    /* A moving thigh must carry the skirt in front of it rather than passing
       between clear endpoints. Corrections remain bounded at the bend cone. */
    GltfModel m=fixture(1,1,0);strcpy(m.humanoid_names[0],"leftUpperLeg");
    m.spring_groups[0].joints[0].stiffness=0.7f;
    m.nodes[2].world_matrix[14]=0.3f;
    spring_bones_init(&m);
    float previous[3]={0,-1,0},displacement=0;
    for(int frame=0;frame<240;frame++) {
        m.nodes[2].world_matrix[14]=0.3f-(frame+1)*0.0025f;
        spring_bones_update(&m,1.0f/120);
        SpringJoint *j=m.spring_groups[0].joints;
        float delta[3];for(int k=0;k<3;k++)delta[k]=j->current_tail[k]-previous[k];
        assert(length(delta)<0.06f);
        assert(spring_bones_stats(&m).max_penetration<0.0002f);
        if(fabsf(j->current_tail[2])>displacement)displacement=fabsf(j->current_tail[2]);
        memcpy(previous,j->current_tail,sizeof(previous));
    }
    assert(displacement>0.1f);cleanup(&m);
    /* Entrance placement is a teleport from bind pose, not a velocity impulse.
       Reset after sampling that pose so tails start beside their attachments. */
    m=fixture(0,0,0);spring_bones_init(&m);
    m.nodes[0].translation[0]=2;
    spring_bones_reset(&m);spring_bones_update(&m,1.0f/120);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[0]-2)<1e-6f);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[1]+1)<1e-6f);
    cleanup(&m);
    /* A garment cannot turn inside out, even when collision-free. The same
       angular restriction must not silently change unrelated hair dynamics. */
    for(int garment=0;garment<2;garment++) {
        m=fixture(garment,0,0);spring_bones_init(&m);
        SpringJoint *j=m.spring_groups[0].joints;
        j->current_tail[1]=j->prev_tail[1]=1;
        spring_bones_update(&m,1.0f/120);
        if(garment)assert(j->current_tail[1]<-0.8191f);
        else assert(j->current_tail[1]>0.999f);
        cleanup(&m);
    }
    /* Successive skirt joints share an attachment-space reference: allowing
       35 degrees relative to each already bent parent still folds the hem up.
       A large obstructing arm may remain unresolved, but cannot lift this hem. */
    m=fixture(1,0,0);
    m.nodes=realloc(m.nodes,5*sizeof(*m.nodes));assert(m.nodes);m.node_count=5;
    memset(m.nodes+3,0,2*sizeof(*m.nodes));
    node(&m.nodes[1],0,-0.1f,0);m.nodes[1].parent_index=0;
    node(&m.nodes[3],0,-0.1f,0);m.nodes[3].parent_index=1;m.nodes[3].world_matrix[13]=-0.2f;
    node(&m.nodes[4],0,-0.1f,0);m.nodes[4].parent_index=3;m.nodes[4].world_matrix[13]=-0.3f;
    m.nodes[1].child_count=m.nodes[3].child_count=1;
    m.nodes[1].children=malloc(sizeof(int));m.nodes[1].children[0]=3;
    m.nodes[3].children=malloc(sizeof(int));m.nodes[3].children[0]=4;
    node(&m.nodes[2],0,-0.15f,0);
    SpringBoneGroup *chain=m.spring_groups;
    chain->bones=realloc(chain->bones,4*sizeof(int));chain->bone_count=4;
    chain->bones[0]=0;chain->bones[1]=1;chain->bones[2]=3;chain->bones[3]=4;
    free(chain->joints);chain->joint_count=3;chain->joints=calloc(3,sizeof(SpringJoint));
    for(int i=0;i<3;i++) {
        chain->joints[i].stiffness=0.7f;chain->joints[i].drag_force=0.4f;chain->joints[i].hit_radius=0.02f;
    }
    spring_bones_init(&m);
    for(int frame=0;frame<600;frame++) {
        spring_bones_update(&m,1.0f/120);
        for(int i=0;i<3;i++) {
            SpringJoint *j=&chain->joints[i];
            float dy=j->current_tail[1]-m.nodes[j->node].world_matrix[13];
            assert(dy/j->bone_length<-0.8191f);
            for(int k=0;k<3;k++)assert(isfinite(j->current_tail[k]));
        }
        assert(chain->joints[2].current_tail[1]<-0.2457f);
    }
    cleanup(&m);
    /* The bend frame follows the attachment, not world-down: a reclining or
       rotated avatar must keep the same garment shape in its own frame. */
    m=fixture(1,0,0);
    m.nodes=realloc(m.nodes,4*sizeof(*m.nodes));assert(m.nodes);m.node_count=4;
    memset(&m.nodes[3],0,sizeof(m.nodes[3]));node(&m.nodes[3],0,0,0);
    m.nodes[3].rotation[0]=m.nodes[3].base_rotation[0]=sqrtf(0.5f);
    m.nodes[3].rotation[3]=m.nodes[3].base_rotation[3]=sqrtf(0.5f);
    m.nodes[3].child_count=1;m.nodes[3].children=malloc(sizeof(int));m.nodes[3].children[0]=0;
    m.nodes[0].parent_index=3;
    spring_bones_reset(&m);spring_bones_init(&m);
    SpringJoint *rotated=m.spring_groups[0].joints;
    rotated->current_tail[1]=rotated->prev_tail[1]=0;
    rotated->current_tail[2]=rotated->prev_tail[2]=1;
    spring_bones_update(&m,1.0f/120);
    assert(rotated->current_tail[2]<-0.8191f);cleanup(&m);
    /* Authored zero gravity remains zero; the collision extension is reversible. */
    m=fixture(1,0,0);g_spring_garment_collisions=0;spring_bones_init(&m);simulate(&m,144,1);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[0])<1e-6f);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[1]+1)<1e-6f);
    assert(spring_bones_stats(&m).garment_arm_penetration>0.1f);
    g_spring_garment_collisions=1;cleanup(&m);
    /* Unrelated hair chains do not acquire the new arm assignments. */
    m=fixture(0,0,0);spring_bones_init(&m);simulate(&m,60,1);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[0])<1e-6f);cleanup(&m);
    /* Legs must not pass through the middle of a skirt segment even when its
       tip is clear. This is the gap left by tail-only thigh contacts. */
    for(int capsule=0;capsule<2;capsule++) {
        m=fixture(1,1,capsule);
        strcpy(m.humanoid_names[0],"leftUpperLeg");
        m.spring_groups[0].joints[0].stiffness=0.7f;
        spring_bones_init(&m);simulate(&m,120,10);
        SpringJoint *j=m.spring_groups[0].joints;
        assert(fabsf(j->current_tail[0])>0.1f);
        assert(fabsf(length(j->current_tail)-1)<1e-5f);
        assert(spring_bones_stats(&m).max_penetration<0.0001f);
        cleanup(&m);
        /* The same authored non-arm volume must still push an overlapping tip. */
        m=fixture(1,1,capsule);
        strcpy(m.humanoid_names[0],"leftUpperLeg");
        node(&m.nodes[2],0,-0.95f,0);
        spring_bones_init(&m);simulate(&m,120,2);
        assert(spring_bones_stats(&m).max_penetration<0.0001f);
        assert(fabsf(m.spring_groups[0].joints[0].current_tail[0])>0.05f);
        cleanup(&m);
    }
    /* Fixed-step dynamics match across 30, 60 and 144 Hz rendering. */
    float reference[3]={0};int rates[]={30,60,144};
    for(int rate=0;rate<3;rate++) {
        m=fixture(0,0,0);SpringJoint *j=m.spring_groups[0].joints;
        j->gravity_power=0.5f;j->gravity_dir[0]=1;j->stiffness=0.25f;
        spring_bones_init(&m);simulate(&m,rates[rate],3);
        if(!rate)memcpy(reference,j->current_tail,sizeof(reference));
        else for(int k=0;k<3;k++)assert(fabsf(reference[k]-j->current_tail[k])<0.0001f);
        spring_bones_update(&m,2);assert(spring_bones_stats(&m).dropped_time>=2);
        assert(isfinite(j->current_tail[0]));cleanup(&m);
    }
    /* Scaled collider radii are not silently left in local-space units. */
    m=fixture(1,0,0);m.nodes[2].world_matrix[0]=m.nodes[2].world_matrix[5]=m.nodes[2].world_matrix[10]=2;
    spring_bones_init(&m);simulate(&m,60,1);
    assert(spring_bones_stats(&m).max_penetration<0.0001f);
    assert(fabsf(m.spring_groups[0].joints[0].current_tail[0])>0.3f);cleanup(&m);
    /* Humanoid capsules cover garments even when no arm collider was authored. */
    m=fixture(1,0,0);
    free(m.spring_colliders[0].colliders);free(m.spring_colliders);m.spring_colliders=NULL;m.spring_collider_count=0;
    m.nodes=realloc(m.nodes,4*sizeof(*m.nodes));assert(m.nodes);m.node_count=4;
    memset(&m.nodes[3],0,sizeof(m.nodes[3]));
    node(&m.nodes[2],0,-0.5f,-0.2f);node(&m.nodes[3],0,-0.5f,0.2f);
    m.humanoid_names=realloc(m.humanoid_names,2*sizeof(*m.humanoid_names));
    m.humanoid_bones=realloc(m.humanoid_bones,2*sizeof(int));
    strcpy(m.humanoid_names[1],"leftHand");m.humanoid_bones[1]=3;m.humanoid_bone_count=2;
    spring_bones_init(&m);assert(spring_bones_stats(&m).generated_arm_colliders==1);
    simulate(&m,60,1);assert(spring_bones_stats(&m).garment_arm_penetration<0.0001f);cleanup(&m);
    /* A collider enclosing an anchored root cannot be solved by stretching a bone. */
    m=fixture(1,0,0);node(&m.nodes[2],0,0,0);
    spring_bones_init(&m);simulate(&m,60,1);
    assert(spring_bones_stats(&m).unresolved_contacts>0);
    assert(fabsf(length(m.spring_groups[0].joints[0].current_tail)-1)<1e-5f);cleanup(&m);
    /* Even a shallow root overlap is impossible for a fixed shaft. Projecting
       its movable closest point used to flip it around the enclosing volume. */
    for(int body=0;body<2;body++) for(int capsule=0;capsule<2;capsule++) {
        m=fixture(1,body,capsule);node(&m.nodes[2],0,-0.08f,0);
        if(body)strcpy(m.humanoid_names[0],"leftUpperLeg");
        m.spring_groups[0].joints[0].stiffness=0.7f;
        spring_bones_init(&m);simulate(&m,120,10);
        SpringJoint *j=m.spring_groups[0].joints;
        assert(spring_bones_stats(&m).unresolved_contacts>0);
        assert(fabsf(j->current_tail[0])<1e-6f);
        assert(fabsf(j->current_tail[1]+1)<1e-6f);
        assert(fabsf(j->current_tail[2])<1e-6f);
        cleanup(&m);
        /* A trapped shaft still gets tip contacts where those are solvable. */
        m=fixture(1,body,capsule);node(&m.nodes[2],0,-0.115f,0);
        if(body)strcpy(m.humanoid_names[0],"leftUpperLeg");
        node(&m.nodes[1],0,-0.23f,0);m.nodes[1].parent_index=0;
        spring_bones_init(&m);simulate(&m,120,10);
        j=m.spring_groups[0].joints;
        float offset[3]={j->current_tail[0],j->current_tail[1]+0.115f,j->current_tail[2]};
        assert(length(offset)>0.1199f);
        assert(fabsf(length(j->current_tail)-0.23f)<1e-5f);
        cleanup(&m);
    }
    /* Opposite directions also work for bones aligned with X, not only Y. */
    m=fixture(0,0,0);node(&m.nodes[1],1,0,0);m.nodes[1].parent_index=0;
    spring_bones_init(&m);
    SpringJoint *opposite=m.spring_groups[0].joints;
    opposite->current_tail[0]=opposite->prev_tail[0]=-1;
    opposite->current_tail[1]=opposite->prev_tail[1]=0;
    spring_bones_update(&m,1.0f/60);
    assert(m.nodes[1].world_matrix[12]<-0.999f);cleanup(&m);
    puts("Spring tests passed: garment arm/body shafts, anchored-root tip fallback, scale, zero gravity, fixed clock and stall recovery");
    return 0;
}
