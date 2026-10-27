/* Reuse the avatar's stencil coverage; never wait on a GPU readback in the UI loop. */
#include "hit_capture.h"
#include <string.h>

void hit_capture_init(HitCapture *c, PlatformWindow *window) {
  memset(c,0,sizeof(*c)); c->window=window;
  platform_set_input_mask(window,NULL,0,0);
}

void hit_capture_reset(HitCapture *c) {
  if (c->mask.bits) {
    input_mask_clear(&c->mask);
    platform_set_input_mask(c->window,NULL,0,0);
  }
  for (int i=0;i<2;i++) if (c->fences[i]) {
    glDeleteSync(c->fences[i]); c->fences[i]=0;
  }
  c->next_capture=0;
}

void hit_capture_resize(HitCapture *c, int width, int height) {
  /* Old GPU transfers refer to the old projection. Preserve/recentre the
   * last visible bitmap until a new one arrives instead of making a visible
   * avatar briefly click-through (which also drops consecutive wheel events). */
  for(int i=0;i<2;i++)if(c->fences[i]) {
    glDeleteSync(c->fences[i]);c->fences[i]=0;
  }
  c->next_capture=0;
  int changed=input_mask_recenter(&c->mask,width,height);
  if(changed>0)platform_set_input_mask(c->window,c->mask.bits,width,height);
}

static void release_capture_buffers(HitCapture *c) {
  for(int i=0;i<2;i++)if(c->fences[i]) {
    glDeleteSync(c->fences[i]);c->fences[i]=0;
  }
  glDeleteBuffers(2,c->buffers);
  glDeleteFramebuffers(1,&c->fbo);
  glDeleteRenderbuffers(1,&c->depth_stencil);
  memset(c->buffers,0,sizeof(c->buffers));c->fbo=c->depth_stencil=0;
  c->next_slot=0;c->next_capture=0;
}

void hit_capture_cleanup(HitCapture *c) {
  hit_capture_reset(c);
  release_capture_buffers(c);
  PlatformWindow *window=c->window;
  memset(c,0,sizeof(*c)); c->window=window;
}

void hit_capture_poll(HitCapture *c) {
  /* Consume in submission order even when both transfers completed together. */
  for (unsigned j=0;j<2;j++) {
    unsigned i=(c->next_slot+j)%2;
    if (!c->fences[i]) continue;
    GLenum status=glClientWaitSync(c->fences[i],0,0);
    if (status==GL_TIMEOUT_EXPIRED) break;
    glDeleteSync(c->fences[i]); c->fences[i]=0;
    if (status==GL_WAIT_FAILED) continue;
    GLint previous;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&previous);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,c->buffers[i]);
    const unsigned char *pixels=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,(size_t)c->width*c->height,GL_MAP_READ_BIT);
    if (pixels) {
      int changed=input_mask_update(&c->mask,pixels,c->width,c->height,c->window_w,c->window_h);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      if (changed>0) platform_set_input_mask(c->window,c->mask.bits,c->mask.width,c->mask.height);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)previous);
  }
}

void hit_capture_submit(HitCapture *c, GLuint source, int w, int h, double now) {
  if (w<=0 || h<=0 || (size_t)w*h>16777216) return;
  int ww,wh;
  platform_get_window_size(c->window,&ww,&wh);
  if (ww<=0 || wh<=0) return;
  GLint read_fbo,draw_fbo,buffer,alignment,renderbuffer;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&buffer);
  glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
  glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
  if (c->width!=w || c->height!=h || c->window_w!=ww || c->window_h!=wh) {
    release_capture_buffers(c);
    c->width=w; c->height=h; c->window_w=ww; c->window_h=wh;
    glGenFramebuffers(1,&c->fbo); glGenRenderbuffers(1,&c->depth_stencil);
    glBindRenderbuffer(GL_RENDERBUFFER,c->depth_stencil);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,c->fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,c->depth_stencil);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glGenBuffers(2,c->buffers);
    for (int i=0;i<2;i++) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER,c->buffers[i]);
      glBufferData(GL_PIXEL_PACK_BUFFER,(size_t)w*h,NULL,GL_STREAM_READ);
    }
  }
  unsigned slot=c->next_slot;
  if (now>=c->next_capture && !c->fences[slot]) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,c->fbo);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,c->fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,c->buffers[slot]);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,w,h,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,0);
    c->fences[slot]=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    c->next_slot=(slot+1)%2;
    c->next_capture=now+1.0/60.0;
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)buffer);
  glPixelStorei(GL_PACK_ALIGNMENT,alignment);
  glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)renderbuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw_fbo);
}
