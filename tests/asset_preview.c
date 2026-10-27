/* Offline visual check of an asset pose using the production loader/renderer. */
#include "platform.h"
#include "renderer.h"
#include "animator.h"
#include "camera.h"
#include "spring_bones.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
  if(argc<5 || argc>7) { fprintf(stderr,"Usage: %s MODEL ANIMATION TIME OUTPUT.ppm [SETTLE_SECONDS|motion [FPS]]\n",argv[0]);return 2; }
  float fps=argc==7 ? (float)atof(argv[6]) : 60;
  if(!isfinite(fps) || fps<10 || fps>240)return 2;
  PlatformWindow *window=platform_create_window(512,512,"Asset preview",true,false);
  if(!window)return 1;
  platform_hide_window(window);
  Renderer renderer={0}; GltfModel model={0}; Animator animator;
  if(!renderer_init(&renderer) || !gltf_load(&model,argv[1]))return 1;
  model.anim_correction_mode=argc>=6 ? 0 : 1;
  if(argc>=6) { gltf_update_transforms(&model);spring_bones_init(&model); }
  if(!gltf_load_animation(&model,argv[2]))return 1;
  animator_init(&animator);animator_set_use_ozz(&animator,true);animator_set_model(&animator,&model);
  int motion=argc>=6 && strcmp(argv[5],"motion")==0;
  animator_set_time(&animator,motion ? 0 : (float)atof(argv[3]));gltf_update_transforms(&model);
  if(argc>=6) {
    spring_bones_reset(&model);
    float duration=fmaxf(0,(float)atof(motion ? argv[3] : argv[5]));
    if(motion)animator_play(&animator,1);else animator_pause(&animator);
    g_spring_gravity=1;
    for(int frame=0;frame<(int)(duration*fps);frame++) {
      if(motion)animator_update(&animator,1.0f/fps);
      gltf_update_transforms(&model);spring_bones_update(&model,1.0f/fps);
    }
    gltf_update_transforms(&model);
  }
  renderer_set_model(&renderer,&model);
  OrbitCamera camera;camera_init(&camera);
  camera.azimuth=argc>=6 ? 3.141593f : 3.8f;
  camera.polar=argc>=6 ? 1.570796f : 1.15f;
  camera.distance=argc>=6 ? 2.4f : 2.9f;camera.target[1]=0.75f;camera_update(&camera);
  GLuint fbo,color,depth;
  glGenFramebuffers(1,&fbo);glGenRenderbuffers(1,&color);glGenRenderbuffers(1,&depth);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glBindRenderbuffer(GL_RENDERBUFFER,color);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,512,512);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
  glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,512,512);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
  glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  renderer_draw(&renderer,&camera,512,512);
  unsigned char *pixels=malloc(512*512*4);
  glReadPixels(0,0,512,512,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  FILE *out=fopen(argv[4],"wb");if(!out)return 1;
  fprintf(out,"P6\n512 512\n255\n");
  for(int y=511;y>=0;y--)for(int x=0;x<512;x++) {
    unsigned char *pixel=pixels+(y*512+x)*4,rgb[3];
    for(int k=0;k<3;k++)rgb[k]=(unsigned char)(pixel[k]+(255-pixel[3])*0.85f);
    fwrite(rgb,1,3,out);
  }
  fclose(out);free(pixels);
  animator_cleanup(&animator);renderer_cleanup(&renderer);gltf_unload(&model);
  glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&color);glDeleteRenderbuffers(1,&depth);
  platform_destroy_window(window);return 0;
}
