/* Real GL regression: shadow reallocations must preserve the caller's MSAA target. */
#include "platform.h"
#include "renderer.h"
#include "camera.h"
#include "hit_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  PlatformWindow *window = platform_create_window(320, 320, "VRM render test", true, false);
  assert(window);
  platform_hide_window(window);
  Renderer renderer = {0};
  assert(renderer_init(&renderer));
  assert(renderer.shadow_program && renderer.shadow_blur_program && renderer.shadow_comp_program);
  GLuint vao, vbo, target, color, resolved, resolved_color, depth, resolved_depth;
  float triangle[] = {-0.5f,-0.5f,0, 0.5f,-0.5f,0, 0,0.5f,0};
  glGenVertexArrays(1,&vao);
  glGenBuffers(1,&vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
  glVertexAttrib3f(1,0,0,1);
  GltfPrimitive primitive = {0};
  primitive.vao=vao; primitive.mode=GL_TRIANGLES; primitive.vertex_count=3; primitive.material_index=-1;
  GltfMesh mesh = {0};
  mesh.primitive_count=1; mesh.primitives=&primitive;
  GltfNode node = {0};
  node.mesh_index=0; node.skin_index=-1;
  node.world_matrix[0]=node.world_matrix[5]=node.world_matrix[10]=node.world_matrix[15]=1;
  GltfModel model = {0};
  model.loaded=true; model.node_count=1; model.nodes=&node; model.mesh_count=1; model.meshes=&mesh;
  renderer_set_model(&renderer,&model);
  renderer.capture_hit_mask=true;
  HitCapture hit;hit_capture_init(&hit,window);
  glGenFramebuffers(1,&target); glGenRenderbuffers(1,&color);
  glGenFramebuffers(1,&resolved); glGenRenderbuffers(1,&resolved_color);
  glGenRenderbuffers(1,&depth); glGenRenderbuffers(1,&resolved_depth);
  GLint max_samples;
  glGetIntegerv(GL_MAX_SAMPLES,&max_samples);
  int samples=max_samples < 4 ? max_samples : 4;
  assert(samples > 0);
  OrbitCamera camera;
  camera_init(&camera);
  for (int frame=0;frame<24;frame++) {
    if(frame) {
      hit_capture_resize(&hit,320,320);
      assert(input_mask_contains(&hit.mask,160,160) && "Resize dropped input on the visible avatar");
      assert(!input_mask_contains(&hit.mask,0,0));
    }
    /* Exercise both allocation and cached-target frames at each size. */
    int w=320+(frame/2)*16, h=320+(frame/2)*8;
    camera.aspect=(float)w/h;
    camera_update(&camera);
    glBindRenderbuffer(GL_RENDERBUFFER,color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH24_STENCIL8,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,w,h);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    renderer_draw(&renderer,&camera,w,h);
    GLint bound;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);
    assert((GLuint)bound == target && "Shadow allocation discarded the MSAA framebuffer");
    hit.next_capture=0; /* Force every test frame through the readback path. */
    hit_capture_submit(&hit,target,w,h,platform_get_time());
    if(frame)assert(input_mask_contains(&hit.mask,160,160) && "GPU resize cleared the last visible input mask");
    glFinish();hit_capture_poll(&hit);
    assert(input_mask_contains(&hit.mask,160,160));
    assert(!input_mask_contains(&hit.mask,0,0));
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);
    assert((GLuint)bound==target && "Hit capture changed the render target");
    glBindRenderbuffer(GL_RENDERBUFFER,resolved_color);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,w,h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolved);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,resolved_color);
    glBindRenderbuffer(GL_RENDERBUFFER,resolved_depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,resolved_depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,resolved);
    unsigned char pixel[4];
    glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[3] > 240 && "Resolved frame lost its visible triangle");
    unsigned char stencil;
    glReadPixels(w/2,h/2,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
    assert(stencil==1 && "Visible triangle is missing from the hit mask");
    glReadPixels(w/2+100,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[3]>0 && "Expected a visible shadow at this point");
    glReadPixels(w/2+100,h/2,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
    assert(stencil==0 && "The shadow must never capture mouse input");
    assert(glGetError()==GL_NO_ERROR);
  }
  /* Fully transparent geometry must neither capture input nor lose its shadow. */
  GltfMaterial material={0};
  material.base_color[0]=material.base_color[1]=material.base_color[2]=1;
  material.alpha_mode=2;
  model.material_count=1; model.materials=&material; primitive.material_index=0;
  int w=320+11*16, h=320+11*8;
  glBindFramebuffer(GL_FRAMEBUFFER,target);
  glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  renderer_draw(&renderer,&camera,w,h);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,target);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolved);
  glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_STENCIL_BUFFER_BIT,GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,resolved);
  unsigned char stencil;
  glReadPixels(w/2,h/2,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
  assert(stencil==0 && "Fully transparent fragments must pass clicks through");
  assert(glGetError()==GL_NO_ERROR);
  renderer_set_model(&renderer,NULL);
  hit_capture_cleanup(&hit);
  renderer_cleanup(&renderer);
  glDeleteRenderbuffers(1,&color); glDeleteRenderbuffers(1,&resolved_color);
  glDeleteRenderbuffers(1,&depth); glDeleteRenderbuffers(1,&resolved_depth);
  glDeleteFramebuffers(1,&target); glDeleteFramebuffers(1,&resolved);
  glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
  platform_destroy_window(window);
  puts("Render target check passed: 24 MSAA resize frames; retained asynchronous avatar input, transparent fragments and shadow exclusion.");
  return 0;
}
