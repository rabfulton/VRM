#include "transition.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static PoseTransform identity(void) {
    PoseTransform pose={{0,0,0},{0,0,0,1},{1,1,1}};
    return pose;
}
static void close_value(float a,float b,float tolerance) { if (fabsf(a-b)>tolerance) { fprintf(stderr,"%g != %g\n",a,b); assert(0); } }
static void rotate_y(PoseTransform *pose,float angle) { pose->rotation[1]=sinf(angle/2);pose->rotation[3]=cosf(angle/2); }

int main(void) {
    PoseTransform source=identity(),target=identity(),out;
    PoseVelocity sv={0},tv={0};
    PoseTransition transition;
    source.translation[0]=2; sv.translation[0]=3; tv.translation[0]=1;
    pose_transition_init(&transition,&source,&sv,&target,&tv);
    pose_transition_sample(&transition,&target,0,0.4f,&out);
    close_value(out.translation[0],2,1e-6f);
    float h=0.0001f;
    target.translation[0]=h;
    pose_transition_sample(&transition,&target,h,0.4f,&out);
    close_value((out.translation[0]-2)/h,3,0.005f);
    target.translation[0]=0.4f;
    pose_transition_sample(&transition,&target,0.4f,0.4f,&out);
    close_value(out.translation[0],target.translation[0],1e-7f);
    target.translation[0]=0.4f-h;
    pose_transition_sample(&transition,&target,0.4f-h,0.4f,&out);
    close_value((0.4f-out.translation[0])/h,1,0.03f);
    pose_transition_sample(&transition,&target,0,0,&out);
    close_value(out.translation[0],target.translation[0],1e-7f);

    source=identity();target=identity();sv=(PoseVelocity){0};tv=(PoseVelocity){0};
    rotate_y(&source,1.2f);rotate_y(&target,-0.7f);
    sv.rotation[1]=2;tv.rotation[1]=-1;
    pose_transition_init(&transition,&source,&sv,&target,&tv);
    pose_transition_sample(&transition,&target,0,0.5f,&out);
    close_value(fabsf(out.rotation[1]*source.rotation[1]+out.rotation[3]*source.rotation[3]),1,1e-5f);
    rotate_y(&target,-0.7f-h);
    pose_transition_sample(&transition,&target,h,0.5f,&out);
    PoseVelocity measured;
    pose_velocity(&source,&out,h,&measured);
    close_value(measured.rotation[1],2,0.01f);
    for (int i=0;i<=100;i++) {
        float t=0.5f*i/100;
        rotate_y(&target,-0.7f-t);
        pose_transition_sample(&transition,&target,t,0.5f,&out);
        float length=0;for(int q=0;q<4;q++)length+=out.rotation[q]*out.rotation[q];
        close_value(length,1,1e-5f);
        assert(isfinite(out.rotation[1]));
    }
    /* Rotation continuity also holds when source/target angular axes differ. */
    source=identity();target=identity();rotate_y(&source,1.2f);
    target.rotation[0]=sinf(-0.35f);target.rotation[3]=cosf(-0.35f);
    sv=(PoseVelocity){0};tv=(PoseVelocity){0};
    sv.rotation[0]=0.7f;sv.rotation[1]=2;sv.rotation[2]=-0.3f;
    tv.rotation[0]=-0.5f;tv.rotation[1]=0.2f;tv.rotation[2]=1;
    pose_transition_init(&transition,&source,&sv,&target,&tv);
    float *q=target.rotation, *w=tv.rotation;
    float derivative[4]={
        0.5f*(w[0]*q[3]+w[1]*q[2]-w[2]*q[1]),
        0.5f*(-w[0]*q[2]+w[1]*q[3]+w[2]*q[0]),
        0.5f*(w[0]*q[1]-w[1]*q[0]+w[2]*q[3]),
        -0.5f*(w[0]*q[0]+w[1]*q[1]+w[2]*q[2])};
    for(int i=0;i<4;i++)q[i]+=h*derivative[i];
    pose_transition_sample(&transition,&target,h,0.5f,&out);
    pose_velocity(&source,&out,h,&measured);
    for(int i=0;i<3;i++)close_value(measured.rotation[i],sv.rotation[i],0.01f);

    /* Opposite quaternion signs describe the same orientation, not a full turn. */
    target=source;for(int i=0;i<4;i++)target.rotation[i]=-target.rotation[i];
    sv=(PoseVelocity){0};tv=(PoseVelocity){0};
    pose_transition_init(&transition,&source,&sv,&target,&tv);
    for(int i=0;i<3;i++)close_value(transition.offset.rotation[i],0,1e-5f);
    puts("Transition tests passed: endpoints, velocity continuity, quaternion normalization and sign invariance");
    return 0;
}
