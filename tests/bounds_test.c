#include "gltf_loader.h"
#include "camera.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void animator_sample_frame(GltfModel *model, int index, float time) {
  model->nodes[0].translation[0] = sinf(time)*2 + index*4;
}

static void identity(float m[16]) { memset(m,0,16*sizeof(float)); m[0]=m[5]=m[10]=m[15]=1; }
static void near(float value,float expected) { assert(fabsf(value-expected)<0.001f); }

int main(void) {
  GltfBounds box={.min={-0.2f,0,-0.1f},.max={1.8f,2,0.3f},.valid=true};
  OrbitCamera camera;
  camera_init(&camera);camera.target[1]=0.8f;camera.aspect=300.0f/550;camera_update(&camera);
  float original_x=camera.proj[0][0]*0.1f/3*300/2;
  float original_y=camera.proj[1][1]*0.1f/3*550/2;
  int w,h;float zoom;
  assert(gltf_fit_viewport(&box,(float *)camera.view,camera.fov,300,550,1920,1080,&w,&h,&zoom));
  assert(w>300 && h>=550);near(zoom,1);
  camera.aspect=(float)w/h;camera.viewport_height_scale=h/550.0f;camera_update(&camera);
  near(camera.proj[0][0]*0.1f/3*w/2,original_x);
  near(camera.proj[1][1]*0.1f/3*h/2,original_y);
  for(int corner=0;corner<8;corner++) {
    vec4 point={0,0,0,1},clip,view;
    for(int k=0;k<3;k++)point[k]=(corner&(1<<k))?box.max[k]:box.min[k];
    glm_mat4_mulv(camera.view,point,view);glm_mat4_mulv(camera.proj,view,clip);
    assert(fabsf(clip[0]/clip[3])<1 && fabsf(clip[1]/clip[3])<1);
  }
  assert(gltf_fit_viewport(&box,(float *)camera.view,camera.fov,300,550,640,480,&w,&h,&zoom));
  assert(w<=640 && h<=480 && zoom<1);
  GltfBounds invalid={0};assert(!gltf_fit_viewport(&invalid,(float *)camera.view,camera.fov,300,550,640,480,&w,&h,&zoom));

  GltfModel model={0};GltfNode nodes[2]={0};GltfMesh mesh={0};GltfPrimitive primitive={0};
  model.nodes=nodes;model.node_count=2;model.meshes=&mesh;model.mesh_count=1;
  int roots[]={0,1};model.scene_roots=roots;model.scene_root_count=2;
  mesh.primitives=&primitive;mesh.primitive_count=1;primitive.local_bounds=box;
  for(int i=0;i<2;i++) {identity(nodes[i].world_matrix);nodes[i].mesh_index=-1;nodes[i].skin_index=-1;nodes[i].parent_index=-1;nodes[i].rotation[3]=1;nodes[i].scale[0]=nodes[i].scale[1]=nodes[i].scale[2]=1;}
  nodes[0].mesh_index=0;GltfBounds bounds;
  gltf_get_world_bounds(&model,&bounds);near(bounds.max[0],1.8f);
  GltfSkin skin={0};GltfBounds joint_box=box;int joint=1;
  model.skins=&skin;model.skin_count=1;nodes[0].skin_index=0;primitive.has_skinning=true;
  skin.joints=&joint;skin.joint_count=1;skin.joint_bounds=&joint_box;nodes[1].world_matrix[12]=3;
  gltf_get_world_bounds(&model,&bounds);near(bounds.max[0],4.8f);
  nodes[0].skin_index=-1;primitive.has_skinning=false;
  GltfAnimation clips[2]={0};clips[0].duration=clips[1].duration=6.2831853f;
  model.animations=clips;model.animation_count=2;nodes[0].translation[0]=0.2f;
  gltf_update_transforms(&model);
  gltf_compute_animation_bounds(&model,&bounds);
  assert(bounds.min[0]<-2.19f && bounds.max[0]>7.79f);
  near(nodes[0].translation[0],0.2f);near(nodes[0].world_matrix[12],0.2f);
  puts("Bounds tests passed: asymmetric poses, pixel-stable expansion, display limits, skinned geometry, all clips and pose restoration");
}
