/*
 * VRM Desktop Overlay Viewer - Main Entry Point (GLFW version)
 */

#define _POSIX_C_SOURCE 200809L
#include <epoxy/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include "../deps/cjson/cJSON.h"
#include "../deps/miniaudio.h"
#include "anim_config.h"
#include "app_paths.h"
#include "animator.h"
#include "audio.h"
#include "camera.h"
#include "event.h"
#include "frame_clock.h"
#include "gem_menu.h"
#include "walk_motion.h"
#include "gltf_loader.h"
#include "ipc.h"
#include "hit_capture.h"
#include "platform.h"
#include "renderer.h"
#include "settings.h"
#include "spring_bones.h"
#include "viseme.h"

#ifdef VRM_USE_OZZ
#include "ozz_bridge.h"
#endif

typedef struct {
  char path[4096];
  struct timespec modified;
  off_t size;
  float correction[4];
  bool neutral_facing;
  GltfBounds bounds;
  bool valid;
} ClipBoundsCache;

typedef struct {
  PlatformWindow *window;
  Renderer renderer;
  OrbitCamera camera;
  GltfModel model;
  Animator animator;
  IpcServer ipc;
  AudioSystem audio;
  VisemeAnalyzer viseme;
  HitCapture hit_capture;
  GLuint msaa_fbo, msaa_color, msaa_depth;
  int msaa_samples, fb_width, fb_height;
  bool visible, always_on_top, model_loaded, auto_resize, running;
  GltfBounds clip_bounds;
  float fit_zoom;
  double last_time;
  float model_position[3], model_rotation[3];
  float model_scale;
  double pending_scroll, last_scroll_time;
  char current_model_path[4096];
  float frame_rate;
  WalkMotion walk;
  char walk_id[49];
  bool walk_pending,walk_started;
  bool walk_absolute;
  double walk_target_x;
  int fit_requested_w,fit_requested_h,walk_goal_w,walk_goal_h;
  int64_t walk_pending_at_ns;
  double walk_requested,walk_nominal_speed,walk_estimate,walk_start_anchor;
  float walk_saved_speed;
  FrameClock frame_clock;
  uint32_t chat_request_seq;
  uint32_t studio_request_seq;
  uint32_t visibility_request_seq;
  uint32_t overlay_menu_seq;
  uint32_t menu_action_seq;
  char menu_action_id[49],menu_action_context[49];
  GemMenu menu;
  int overlay_menu_x, overlay_menu_y;
  ClipBoundsCache bounds_cache[32];
  unsigned bounds_cache_cursor;
  double last_load_ms, last_bounds_ms;
  double frame_ms, peak_frame_ms, peak_ipc_ms, peak_update_ms, peak_render_ms, peak_swap_ms;
  uint64_t slow_frames, window_resizes;
  char current_anim_path[4096];
  bool has_current_anim;
  bool animation_neutral_facing;
  bool animation_startup, animation_idle, animation_exit;
  bool audio_stream_open;
  bool presentation_pending;
  bool spring_pose_pending; /* Initialize tails from the first animated pose. */
  bool speech_pose_active;
  // Auto-blink state
  bool auto_blink_enabled;
  float auto_blink_rate;     // Blinks per minute
  float auto_blink_duration; // Blink duration in ms
  uint32_t auto_blink_event_id;
} VrmApp;

static VrmApp app = {0};
static const char *SETTINGS_PATH;

static int model_mouse_x = 0, model_mouse_y = 0; /* For model window */
extern float g_spring_stiffness, g_spring_drag, g_spring_gravity;
extern int g_spring_cloth_mode;
extern float g_spring_cloth_stiffness, g_spring_cloth_drag,
    g_spring_cloth_gravity, g_spring_cloth_taper;
extern float g_anim_seam_blend_time_default;

static void build_view_with_model_transform(float out_view[16]);
static bool model_hit_test(int x, int y);
static bool save_springs_to_config(void);
static bool walking(void) { return app.walk_pending || walk_motion_active(&app.walk); }
static void cancel_walk(void) {
  bool active=walking();
  if(active) {
    app.walk_pending=false;
    walk_motion_cancel(&app.walk);
    app.model_rotation[1]=(float)app.walk.base_yaw;
    renderer_set_model_transform(&app.renderer,app.model_position,app.model_rotation,app.model_scale);
    animator_pause(&app.animator);
    app.animator.speed=app.walk_saved_speed;
  }
  if(app.walk_id[0])app.walk.phase=WALK_CANCELLED;
}

static void set_always_on_top(bool enabled) {
  app.always_on_top = enabled;
  platform_set_always_on_top(app.window, enabled);
  Settings settings = {0};
  settings_load(SETTINGS_PATH, &settings);
  settings.has_always_on_top = true;
  settings.always_on_top = enabled;
  settings_save(SETTINGS_PATH, &settings);
}

static void apply_animation_metadata(const AnimConfig *cfg) {
  animator_set_loop_policy(&app.animator, cfg->has_loopable, cfg->loopable);
  app.animation_startup = cfg->has_startup && cfg->startup;
  app.animation_idle = cfg->has_is_idle && cfg->is_idle;
  app.animation_exit = cfg->has_is_exit && cfg->is_exit;
  bool allowed=!cfg->has_floor_contact || cfg->floor_contact;
  if (app.animator.foot_lock.clip_allowed!=allowed) foot_lock_reset(&app.animator.foot_lock);
  app.animator.foot_lock.clip_allowed=allowed;
}

/* Optional readback diagnostic for clipping regressions; not part of the frame loop. */
static void respond_frame_bounds(int id) {
  int w = app.fb_width, h = app.fb_height;
  if (w <= 0 || h <= 0 || (size_t)w*h > 16777216) {
    ipc_respond(&app.ipc,id,false,"Framebuffer is unavailable or too large to inspect"); return;
  }
  unsigned char *pixels = malloc((size_t)w*h*4);
  if (!pixels) { ipc_respond(&app.ipc,id,false,"Out of memory"); return; }
  GLint framebuffer, draw_framebuffer, renderbuffer, buffer, alignment;
  GLuint capture_fbo=0, capture_color=0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&framebuffer);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_framebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
  glGetIntegerv(GL_READ_BUFFER,&buffer);
  glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
  if (app.msaa_fbo) {
    /* Resolve offscreen: front-buffer ownership is undefined under another
     * desktop window, which would make a readback test report a blank avatar. */
    glGenFramebuffers(1,&capture_fbo);
    glGenRenderbuffers(1,&capture_color);
    glBindRenderbuffer(GL_RENDERBUFFER,capture_color);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,w,h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,capture_fbo);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,capture_color);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,app.msaa_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,capture_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
  } else {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glReadBuffer(GL_FRONT);
  }
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  glPixelStorei(GL_PACK_ALIGNMENT,alignment);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)framebuffer);
  glReadBuffer((GLenum)buffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw_framebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)renderbuffer);
  if(capture_fbo) glDeleteFramebuffers(1,&capture_fbo);
  if(capture_color) glDeleteRenderbuffers(1,&capture_color);
  int min_x=w,min_y=h,max_x=-1,max_y=-1,count=0,edges=0;
  for (int y=0;y<h;y++) for(int x=0;x<w;x++) {
    if (pixels[((size_t)y*w+x)*4+3] <= 2) continue;
    count++;
    if(x<min_x) min_x=x;
    if(x>max_x) max_x=x;
    if(y<min_y) min_y=y;
    if(y>max_y) max_y=y;
    if(x==0 || y==0 || x==w-1 || y==h-1) edges++;
  }
  free(pixels);
  cJSON *data=cJSON_CreateObject();
  cJSON_AddNumberToObject(data,"width",w); cJSON_AddNumberToObject(data,"height",h);
  GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
  cJSON_AddNumberToObject(data,"viewport_width",viewport[2]);
  cJSON_AddNumberToObject(data,"viewport_height",viewport[3]);
  cJSON_AddNumberToObject(data,"projection_height_scale",app.camera.viewport_height_scale);
  cJSON_AddNumberToObject(data,"aspect",app.camera.aspect);
  cJSON_AddNumberToObject(data,"fit_zoom",app.fit_zoom);
  GltfBounds live; gltf_get_world_bounds(&app.model,&live);
  for(int k=0;k<3;k++) {
    char key[16]; snprintf(key,sizeof(key),"world_min_%d",k); cJSON_AddNumberToObject(data,key,live.min[k]);
    snprintf(key,sizeof(key),"world_max_%d",k); cJSON_AddNumberToObject(data,key,live.max[k]);
  }
  cJSON_AddNumberToObject(data,"visible_pixels",count); cJSON_AddNumberToObject(data,"edge_pixels",edges);
  cJSON_AddNumberToObject(data,"min_x",count?min_x:-1); cJSON_AddNumberToObject(data,"max_x",max_x);
  cJSON_AddNumberToObject(data,"min_y",count?h-1-max_y:-1); cJSON_AddNumberToObject(data,"max_y",count?h-1-min_y:-1);
  char *json=cJSON_PrintUnformatted(data); ipc_respond_data(&app.ipc,id,json);
  free(json); cJSON_Delete(data);
}

static bool save_springs_to_config(void) {
  if (!app.has_current_anim)
    return false;
  AnimConfig cfg = {0};
  cfg.spring_stiffness = g_spring_stiffness;
  cfg.spring_drag = g_spring_drag;
  cfg.spring_gravity = g_spring_gravity;
  cfg.has_spring_stiffness = true;
  cfg.has_spring_drag = true;
  cfg.has_spring_gravity = true;
  cfg.spring_cloth_mode = (float)g_spring_cloth_mode;
  cfg.spring_cloth_stiffness = g_spring_cloth_stiffness;
  cfg.spring_cloth_drag = g_spring_cloth_drag;
  cfg.spring_cloth_gravity = g_spring_cloth_gravity;
  cfg.spring_cloth_taper = g_spring_cloth_taper;
  cfg.has_spring_cloth_mode = true;
  cfg.has_spring_cloth_stiffness = true;
  cfg.has_spring_cloth_drag = true;
  cfg.has_spring_cloth_gravity = true;
  cfg.has_spring_cloth_taper = true;
  cfg.anim_seam_blend_time = g_anim_seam_blend_time_default;
  cfg.has_anim_seam_blend_time = true;
  return anim_config_save(app.current_anim_path, &cfg);
}

static void msaa_cleanup(VrmApp *s) {
  if (s->msaa_color) {
    glDeleteRenderbuffers(1, &s->msaa_color);
    s->msaa_color = 0;
  }
  if (s->msaa_depth) {
    glDeleteRenderbuffers(1, &s->msaa_depth);
    s->msaa_depth = 0;
  }
  if (s->msaa_fbo) {
    glDeleteFramebuffers(1, &s->msaa_fbo);
    s->msaa_fbo = 0;
  }
}

static void msaa_resize(VrmApp *s, int w, int h) {
  if (w <= 0 || h <= 0 || s->msaa_samples <= 0)
    return;
  if (s->fb_width == w && s->fb_height == h && s->msaa_fbo)
    return;
  msaa_cleanup(s);
  glGenFramebuffers(1, &s->msaa_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, s->msaa_fbo);
  glGenRenderbuffers(1, &s->msaa_color);
  glBindRenderbuffer(GL_RENDERBUFFER, s->msaa_color);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, s->msaa_samples, GL_RGBA8,
                                   w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, s->msaa_color);
  glGenRenderbuffers(1, &s->msaa_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, s->msaa_depth);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, s->msaa_samples,
                                   GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, s->msaa_depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "MSAA framebuffer incomplete\n");
    msaa_cleanup(s);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  s->fb_width = w;
  s->fb_height = h;
}

static void fit_overlay_window(bool allow_shrink) {
  if (!app.auto_resize || !app.model_loaded || walking()) return;
  int old_x, old_y, old_w, old_h;
  platform_get_window_pos(app.window, &old_x, &old_y);
  platform_get_window_size(app.window, &old_w, &old_h);
  int base_w = (int)fmaxf(100,300*app.model_scale);
  int base_h = (int)fmaxf(100,550*app.model_scale);
  int work_x, work_y, max_w, max_h;
  platform_get_window_workarea(app.window,&work_x,&work_y,&max_w,&max_h);
  (void)work_x; (void)work_y;
  if (max_w < 100 || max_h < 100) { max_w = 1920; max_h = 1080; }
  /* Bound allocation even on very large multi-monitor workareas. */
  if (max_w > 4096) max_w = 4096;
  if (max_h > 4096) max_h = 4096;
  GltfBounds bounds = app.clip_bounds, live;
  gltf_get_world_bounds(&app.model,&live);
  gltf_bounds_merge(&bounds,&live);
  camera_update(&app.camera);
  float view_model[16];
  build_view_with_model_transform(view_model);
  int w = base_w, h = base_h;
  float zoom = 1;
  if (!gltf_fit_viewport(&bounds,view_model,app.camera.fov,base_w,base_h,max_w,max_h,&w,&h,&zoom)) return;
  if (!allow_shrink) {
    bool zooming=app.last_scroll_time>0 && platform_get_time()-app.last_scroll_time<0.25;
    if (w < old_w) w = old_w;
    if (h < old_h) h = old_h;
    if (!zooming && app.fit_zoom > 0 && zoom > app.fit_zoom) zoom = app.fit_zoom;
    /* Grow in chunks: a few pixels of spring movement must not repeatedly
     * reallocate MSAA/shadow buffers and ask the compositor for a new surface. */
    float chunk=zooming ? 256 : 64;
    float reserve=zooming ? 1.15f : 1;
    if (w > old_w) w = (int)fminf(max_w, ceilf(w*reserve/chunk)*chunk);
    if (h > old_h) h = (int)fminf(max_h, ceilf(h*reserve/chunk)*chunk);
  }
  app.fit_zoom = zoom;
  app.fit_requested_w=w;app.fit_requested_h=h;
  app.camera.viewport_height_scale = h / (base_h*zoom);
  app.camera.aspect = (float)w/h;
  if (w != old_w || h != old_h) {
    app.window_resizes++;
    /* Preserve the screen-space camera centre and pixel density. */
    platform_set_window_rect(app.window,old_x+(old_w-w)/2,old_y+(old_h-h)/2,w,h);
  }
}

static double avatar_height_pixels(void) {
  double height=app.clip_bounds.valid?app.clip_bounds.max[1]-app.clip_bounds.min[1]:1.7;
  double base_h=fmax(100,550*app.model_scale);
  double pixels=height*app.model_scale*base_h*fmax(app.fit_zoom,.01)/(2*tan(app.camera.fov*.5)*fmax(app.camera.distance,.1));
  return fmax(60,fmin(4000,pixels));
}

static void prepare_walk_tick(int64_t now_ns) {
  if(!app.walk_pending)return;
  int x,y,w,h;platform_get_window_pos(app.window,&x,&y);platform_get_window_size(app.window,&w,&h);
  if((w!=app.walk_goal_w || h!=app.walk_goal_h) && now_ns-app.walk_pending_at_ns<350000000LL)return;
  int wx,wy,ww,wh;platform_get_window_workarea(app.window,&wx,&wy,&ww,&wh);
  (void)wy;(void)wh;
  /* Absolute targets use the camera/viewport centre, independent of fitting.
   * Relative requests never reverse direction to correct an offscreen start. */
  if(app.walk_absolute)app.walk_requested=app.walk_target_x-(x+w*.5);
  double goal=walk_motion_target(x,w,wx,ww,app.walk_absolute?app.walk_target_x:app.walk_requested,app.walk_absolute);
  double facing=walk_motion_heading(app.camera.azimuth,goal-x);
  app.walk_start_anchor=x+w*.5;
  walk_motion_begin(&app.walk,now_ns,x,y,goal-x,app.walk_nominal_speed,app.walk.base_yaw,facing);
  app.walk_estimate=walk_motion_seconds(&app.walk);app.walk_pending=false;app.walk_started=true;
  if(!walk_motion_active(&app.walk)) {animator_pause(&app.animator);app.animator.speed=app.walk_saved_speed;}
}

static void respond_walk_state(cJSON *parent) {
  int x,y,w,h;platform_get_window_pos(app.window,&x,&y);platform_get_window_size(app.window,&w,&h);
  int wx,wy,ww,wh;platform_get_window_workarea(app.window,&wx,&wy,&ww,&wh);
  cJSON *rect=cJSON_AddObjectToObject(parent,"window");
  cJSON_AddNumberToObject(rect,"x",x);cJSON_AddNumberToObject(rect,"y",y);
  cJSON_AddNumberToObject(rect,"width",w);cJSON_AddNumberToObject(rect,"height",h);
  cJSON *area=cJSON_AddObjectToObject(parent,"workarea");
  cJSON_AddNumberToObject(area,"x",wx);cJSON_AddNumberToObject(area,"y",wy);
  cJSON_AddNumberToObject(area,"width",ww);cJSON_AddNumberToObject(area,"height",wh);
  cJSON_AddBoolToObject(parent,"humanoid",app.model_loaded && app.model.humanoid_bone_count>0);
  cJSON_AddNumberToObject(parent,"avatar_height_pixels",avatar_height_pixels());
  cJSON *motion=cJSON_AddObjectToObject(parent,"walk");
  cJSON_AddStringToObject(motion,"id",app.walk_id);
  cJSON_AddStringToObject(motion,"mode",app.walk_absolute?"absolute":"relative");
  cJSON_AddNumberToObject(motion,"x",x+w*.5);
  cJSON_AddNumberToObject(motion,"start_x",app.walk_start_anchor);
  cJSON_AddNumberToObject(motion,"requested_x",app.walk_absolute?app.walk_target_x:app.walk_start_anchor+app.walk_requested);
  cJSON_AddNumberToObject(motion,"target_x",app.walk_started?app.walk_start_anchor+app.walk.distance:
      app.walk_absolute?app.walk_target_x:app.walk_start_anchor+app.walk_requested);
  cJSON_AddStringToObject(motion,"phase",app.walk_pending?"preparing":walk_motion_phase(&app.walk));
  cJSON_AddNumberToObject(motion,"progress",app.walk.progress);
  cJSON_AddNumberToObject(motion,"yaw",app.walk.yaw);
  cJSON_AddNumberToObject(motion,"remaining_seconds",app.walk_pending?app.walk_estimate:app.walk.remaining);
  cJSON_AddNumberToObject(motion,"duration_seconds",app.walk_estimate);
  cJSON_AddNumberToObject(motion,"requested_distance",app.walk_requested);
  cJSON_AddNumberToObject(motion,"distance",app.walk_started?app.walk.distance:app.walk_requested);
  cJSON_AddNumberToObject(motion,"actual_distance",app.walk_started?x+w*.5-app.walk_start_anchor:0);
  cJSON_AddBoolToObject(motion,"clamped",app.walk_started && fabs(app.walk.distance-app.walk_requested)>.5);
}

static void update_window_for_zoom(void) { fit_overlay_window(true); }

static void refit_animation_window(void) {
  if (!app.model_loaded) return;
  struct stat info;
  bool can_cache = app.has_current_anim && stat(app.current_anim_path,&info) == 0;
  ClipBoundsCache *entry = NULL;
  if (can_cache) for (unsigned i=0; i<32; i++) {
    ClipBoundsCache *candidate = &app.bounds_cache[i];
    if (candidate->valid && strcmp(candidate->path,app.current_anim_path)==0 &&
        candidate->neutral_facing==app.animation_neutral_facing &&
        candidate->size==info.st_size && candidate->modified.tv_sec==info.st_mtim.tv_sec &&
        candidate->modified.tv_nsec==info.st_mtim.tv_nsec &&
        memcmp(candidate->correction,app.model.anim_global_correction,sizeof(candidate->correction))==0) {
      entry = candidate; break;
    }
  }
  app.last_bounds_ms = 0;
  if (entry) app.clip_bounds = entry->bounds;
  else {
    double start = platform_get_time();
    gltf_compute_animation_bounds(&app.model,&app.clip_bounds);
    app.last_bounds_ms = (platform_get_time()-start)*1000;
    if (can_cache) {
      entry = &app.bounds_cache[app.bounds_cache_cursor++ % 32];
      snprintf(entry->path,sizeof(entry->path),"%s",app.current_anim_path);
      entry->modified = info.st_mtim; entry->size = info.st_size;
      entry->neutral_facing = app.animation_neutral_facing;
      memcpy(entry->correction,app.model.anim_global_correction,sizeof(entry->correction));
      entry->bounds = app.clip_bounds; entry->valid = true;
    }
  }
  /* Keep the viewport stable between clips; zoom/camera controls can refit it. */
  fit_overlay_window(false);
}

static void update_anim_correction_mode(int mode) {
  app.model.anim_correction_mode = mode;
  float *c = app.model.anim_global_correction,
        *a = app.model.anim_global_auto_correction;
  if (mode == 0) {
    c[0] = 0;
    c[1] = 0;
    c[2] = 0;
    c[3] = 1;
    printf("Anim correction: none\n");
  } else if (mode == 1) {
    c[0] = a[0];
    c[1] = a[1];
    c[2] = a[2];
    c[3] = a[3];
    printf("Anim correction: auto\n");
  } else if (mode == 2) {
    c[0] = 1;
    c[1] = 0;
    c[2] = 0;
    c[3] = 0;
    printf("Anim correction: rotX 180\n");
  } else if (mode == 3) {
    c[0] = 0;
    c[1] = 1;
    c[2] = 0;
    c[3] = 0;
    printf("Anim correction: rotY 180\n");
  } else if (mode == 4) {
    c[0] = 0;
    c[1] = 0;
    c[2] = 1;
    c[3] = 0;
    printf("Anim correction: rotZ 180\n");
  }
  fflush(stdout);
}

static void rebuild_ozz_after_correction_change(bool keep_snapshot) {
  (void)keep_snapshot;
#ifdef VRM_USE_OZZ
  if (animator_get_use_ozz(&app.animator)) {
    animator_rebuild_ozz(&app.animator, keep_snapshot);
  }
#endif
}

/* Base64 decoder helper */
static int b64_char_value(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

/* Strict base64 decoding: reject malformed padding and incomplete PCM upstream. */
static size_t base64_decode(const char *src, size_t len, uint8_t *out) {
  if (len % 4 != 0) return SIZE_MAX;
  size_t written = 0;
  for (size_t i = 0; i < len; i += 4) {
    int a = b64_char_value(src[i]), b = b64_char_value(src[i + 1]);
    bool pad_c = src[i + 2] == '=', pad_d = src[i + 3] == '=';
    int c = pad_c ? 0 : b64_char_value(src[i + 2]);
    int d = pad_d ? 0 : b64_char_value(src[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || (pad_c && !pad_d) ||
        ((pad_c || pad_d) && i + 4 != len) ||
        (pad_c && (b & 15)) || (pad_d && !pad_c && (c & 3))) return SIZE_MAX;
    uint32_t v = (uint32_t)a << 18 | (uint32_t)b << 12 | (uint32_t)c << 6 | (uint32_t)d;
    out[written++] = (uint8_t)(v >> 16);
    if (!pad_c) out[written++] = (uint8_t)(v >> 8);
    if (!pad_d) out[written++] = (uint8_t)v;
  }
  return written;
}

/* ============================================================================
 * Auto-Blink Processing
 * ============================================================================
 */

// Simple pseudo-random number generator (xorshift32)
static uint32_t g_rand_state = 12345;
static uint32_t rand_u32(void) {
  g_rand_state ^= g_rand_state << 13;
  g_rand_state ^= g_rand_state >> 17;
  g_rand_state ^= g_rand_state << 5;
  return g_rand_state;
}

static float rand_float(void) { return (float)rand_u32() / (float)UINT32_MAX; }

// Generate random blink interval using exponential distribution
// Given rate in blinks/minute, returns interval in nanoseconds
static int64_t random_blink_interval_ns(float rate_per_minute) {
  if (rate_per_minute <= 0)
    rate_per_minute = 14.0f;
  float rate_per_second = rate_per_minute / 60.0f;

  // Exponential distribution: -ln(U) / λ where U is uniform(0,1)
  float u = rand_float();
  if (u < 1e-6f)
    u = 1e-6f; // Avoid log(0)
  float interval_seconds = -logf(u) / rate_per_second;

  // Clamp to reasonable range: 0.5s to 10s
  if (interval_seconds < 0.5f)
    interval_seconds = 0.5f;
  if (interval_seconds > 10.0f)
    interval_seconds = 10.0f;

  return (int64_t)(interval_seconds * 1e9);
}

// Schedule a new blink event
static uint32_t schedule_blink(int64_t start_time_ns, float duration_ms) {
  Event blink = {0};
  blink.type = EVENT_TYPE_BLINK;
  blink.track_id = 0;
  blink.domain = EVENT_DOMAIN_SECONDS;
  blink.start_time = start_time_ns;
  blink.duration = event_ms_to_ns((int64_t)duration_ms);
  blink.loop_count =
      0; // Single shot - we reschedule manually for random timing
  blink.flags = EVENT_FLAG_SUBSYSTEM_OWNS_TIMING;

  return event_schedule(&blink);
}

// Blink event processor callback
static void process_blink(Event *e, void *ctx) {
  VrmApp *a = (VrmApp *)ctx;

  if (e->state == EVENT_STATE_CANCELED) {
    // Cleanup - open eyes
    if (a->model_loaded) {
      gltf_set_expression(&a->model, "Blink", 0.0f);
    }
    event_mark_complete(e->id);
    a->auto_blink_event_id = 0;
    return;
  }

  if (e->state != EVENT_STATE_ACTIVE)
    return;
  if (!a->model_loaded)
    return;

  // Set blink expression
  gltf_set_expression(&a->model, "Blink", 1.0f);

  // Check if blink duration is complete
  int64_t now_ns = (int64_t)(platform_get_time() * 1e9);
  if (now_ns >= e->start_time + e->duration) {
    // Open eyes
    gltf_set_expression(&a->model, "Blink", 0.0f);
    event_mark_complete(e->id);

    // Schedule next blink if still enabled
    if (a->auto_blink_enabled) {
      int64_t next_interval = random_blink_interval_ns(a->auto_blink_rate);
      a->auto_blink_event_id =
          schedule_blink(now_ns + next_interval, a->auto_blink_duration);
    } else {
      a->auto_blink_event_id = 0;
    }
  }
}

// Start auto-blinking
static void auto_blink_start(void) {
  if (app.auto_blink_event_id != 0)
    return; // Already running

  int64_t now_ns = (int64_t)(platform_get_time() * 1e9);
  int64_t first_interval = random_blink_interval_ns(app.auto_blink_rate);
  app.auto_blink_event_id =
      schedule_blink(now_ns + first_interval, app.auto_blink_duration);
  printf("Auto-blink started (rate=%.1f/min, duration=%.0fms, first_id=%u)\n",
         app.auto_blink_rate, app.auto_blink_duration, app.auto_blink_event_id);
}

// Stop auto-blinking
static void auto_blink_stop(void) {
  if (app.auto_blink_event_id != 0) {
    event_cancel(app.auto_blink_event_id);
    app.auto_blink_event_id = 0;
  }
  // Ensure eyes are open
  if (app.model_loaded) {
    gltf_set_expression(&app.model, "Blink", 0.0f);
  }
  printf("Auto-blink stopped\n");
}

/* A read-only snapshot used by external controllers; the render thread owns it. */
static void respond_state(int id) {
  cJSON *state = cJSON_CreateObject();
  cJSON_AddNumberToObject(state, "api_version", 1);
  cJSON_AddBoolToObject(state, "model_loaded", app.model_loaded);
  cJSON_AddStringToObject(state, "model_path", app.current_model_path);
  cJSON_AddStringToObject(state, "animation_path", app.current_anim_path);
  cJSON_AddBoolToObject(state, "animation_neutral_facing", app.animation_neutral_facing);
  cJSON_AddBoolToObject(state, "playing", app.animator.playing);
  cJSON_AddNumberToObject(state, "animation_index", app.animator.current_animation);
  cJSON_AddNumberToObject(state, "time", app.animator.time);
  cJSON_AddNumberToObject(state, "speed", app.animator.speed);
  cJSON_AddBoolToObject(state, "loop", app.animator.loop_remaining < 0);
  cJSON *properties = cJSON_AddObjectToObject(state, "animation_properties");
  cJSON_AddBoolToObject(properties, "loopable", !app.animator.loop_policy_set || app.animator.loop_allowed);
  cJSON_AddBoolToObject(properties, "explicit_loopable", app.animator.loop_policy_set);
  cJSON_AddBoolToObject(properties, "startup", app.has_current_anim && app.animation_startup);
  cJSON_AddBoolToObject(properties, "is_idle", app.has_current_anim && app.animation_idle);
  cJSON_AddBoolToObject(properties, "is_exit", app.has_current_anim && app.animation_exit);
  cJSON_AddBoolToObject(properties,"floor_contact",app.animator.foot_lock.clip_allowed);
  cJSON_AddStringToObject(state, "backend", animator_get_use_ozz(&app.animator) ? "ozz" : "legacy");
#ifdef VRM_USE_OZZ
  cJSON_AddBoolToObject(state, "ozz_available", true);
#else
  cJSON_AddBoolToObject(state, "ozz_available", false);
#endif
  cJSON_AddNumberToObject(state, "frame_rate", app.frame_rate);
  cJSON_AddNumberToObject(state, "frame_rate_limit", 60);
  cJSON_AddNumberToObject(state, "chat_request_seq", app.chat_request_seq);
  cJSON_AddNumberToObject(state, "studio_request_seq", app.studio_request_seq);
  cJSON_AddNumberToObject(state, "visibility_request_seq", app.visibility_request_seq);
  cJSON_AddStringToObject(state, "window_backend", platform_backend_name());
  cJSON_AddNumberToObject(state, "overlay_menu_seq", app.overlay_menu_seq);
  cJSON_AddNumberToObject(state, "menu_action_seq", app.menu_action_seq);
  cJSON_AddStringToObject(state, "menu_action_id", app.menu_action_id);
  cJSON_AddStringToObject(state, "menu_action_context", app.menu_action_context);
  cJSON *menu = cJSON_AddObjectToObject(state, "overlay_menu_position");
  cJSON_AddNumberToObject(menu, "x", app.overlay_menu_x);
  cJSON_AddNumberToObject(menu, "y", app.overlay_menu_y);
  cJSON *timing = cJSON_AddObjectToObject(state, "frame_timing");
  cJSON_AddNumberToObject(timing, "frame_ms", app.frame_ms);
  cJSON_AddNumberToObject(timing, "peak_frame_ms", app.peak_frame_ms);
  cJSON_AddNumberToObject(timing, "peak_ipc_ms", app.peak_ipc_ms);
  cJSON_AddNumberToObject(timing, "peak_update_ms", app.peak_update_ms);
  cJSON_AddNumberToObject(timing, "peak_render_ms", app.peak_render_ms);
  cJSON_AddNumberToObject(timing, "peak_swap_ms", app.peak_swap_ms);
  cJSON_AddNumberToObject(timing, "slow_frames", (double)app.slow_frames);
  cJSON_AddNumberToObject(timing, "window_resizes", (double)app.window_resizes);
  cJSON_AddNumberToObject(timing, "last_load_ms", app.last_load_ms);
  cJSON_AddNumberToObject(timing, "last_bounds_ms", app.last_bounds_ms);
  cJSON_AddBoolToObject(state, "visible", app.visible);
  cJSON_AddBoolToObject(state, "presentation_pending", app.presentation_pending);
  cJSON_AddBoolToObject(state, "always_on_top", app.always_on_top);
  cJSON_AddBoolToObject(state, "auto_resize", app.auto_resize);
  cJSON_AddNumberToObject(state, "scale", app.model_scale);
  cJSON_AddStringToObject(state, "shader", app.renderer.shader_mode == RENDERER_SHADER_ANIME ? "anime" :
                          app.renderer.shader_mode == RENDERER_SHADER_TOON ? "toon" : "pbr");
  cJSON_AddBoolToObject(state, "lip_sync", app.viseme.enabled);
  cJSON *face = cJSON_AddObjectToObject(state, "face");
  VrmExpressionMap *expressions_map = app.model.vrm_expressions;
  cJSON_AddNumberToObject(face, "emotion_scale",
      expressions_map && expressions_map->balance_initialized ? expressions_map->emotion_scale : 1.0f);
  cJSON_AddBoolToObject(face, "speech_active",
      app.viseme.enabled && (app.audio_stream_open || audio_available(&app.audio) > 0));
  cJSON_AddNumberToObject(state, "correction", app.model_loaded ? app.model.anim_correction_mode : 0);
  cJSON *clips = cJSON_AddArrayToObject(state, "clips");
  float duration = 0.0f;
  if (app.model_loaded) {
    for (int i = 0; i < app.model.animation_count; i++) {
      cJSON *clip = cJSON_CreateObject();
      cJSON_AddNumberToObject(clip, "index", i);
      cJSON_AddStringToObject(clip, "name", app.model.animations[i].name);
      cJSON_AddNumberToObject(clip, "duration", app.model.animations[i].duration);
      cJSON_AddItemToArray(clips, clip);
      if (i == app.animator.current_animation) duration = app.model.animations[i].duration;
    }
  }
  cJSON_AddNumberToObject(state, "duration", duration);
  cJSON *expressions = cJSON_AddArrayToObject(state, "expressions");
  if (app.model_loaded) {
    char names[MAX_VRM_EXPRESSIONS][VRM_EXPRESSION_NAME_LEN];
    int count = gltf_get_expression_names(&app.model, names, MAX_VRM_EXPRESSIONS);
    for (int i = 0; i < count; i++) cJSON_AddItemToArray(expressions, cJSON_CreateString(names[i]));
  }
  cJSON *spring = cJSON_AddObjectToObject(state, "spring");
  cJSON_AddNumberToObject(spring, "stiffness", g_spring_stiffness);
  cJSON_AddNumberToObject(spring, "drag", g_spring_drag);
  cJSON_AddNumberToObject(spring, "gravity", g_spring_gravity);
  cJSON_AddBoolToObject(spring, "cloth_mode", g_spring_cloth_mode);
  cJSON_AddNumberToObject(spring, "cloth_stiffness", g_spring_cloth_stiffness);
  cJSON_AddNumberToObject(spring, "cloth_drag", g_spring_cloth_drag);
  cJSON_AddNumberToObject(spring, "cloth_gravity", g_spring_cloth_gravity);
  cJSON_AddNumberToObject(spring, "cloth_taper", g_spring_cloth_taper);
  SpringStats stats = spring_bones_stats(&app.model);
  cJSON *physics = cJSON_AddObjectToObject(state, "physics");
  cJSON_AddBoolToObject(physics, "garment_collisions", g_spring_garment_collisions);
  cJSON_AddNumberToObject(physics, "garment_chains", stats.garment_chains);
  cJSON_AddNumberToObject(physics, "arm_colliders", stats.arm_colliders);
  cJSON_AddNumberToObject(physics, "generated_arm_colliders", stats.generated_arm_colliders);
  cJSON_AddNumberToObject(physics, "contacts", stats.contacts);
  cJSON_AddNumberToObject(physics, "unresolved_contacts", stats.unresolved_contacts);
  cJSON_AddNumberToObject(physics, "max_penetration", stats.max_penetration);
  cJSON_AddNumberToObject(physics, "garment_arm_penetration", stats.garment_arm_penetration);
  cJSON_AddNumberToObject(physics, "steps", stats.steps);
  cJSON_AddNumberToObject(physics, "dropped_time", stats.dropped_time);
  cJSON *planting = cJSON_AddObjectToObject(state, "foot_lock");
  cJSON_AddBoolToObject(planting, "enabled", app.animator.foot_lock.enabled);
  cJSON_AddBoolToObject(planting,"clip_allowed",app.animator.foot_lock.clip_allowed);
  cJSON_AddNumberToObject(planting, "pelvis_offset", app.animator.foot_lock.pelvis_offset);
  cJSON_AddNumberToObject(planting, "floor_offset", app.animator.foot_lock.floor_offset);
  int supported = 0;
  for (int i = 0; i < 2; i++) {
    const FootPlant *foot = &app.animator.foot_lock.feet[i];
    cJSON *entry = cJSON_AddObjectToObject(planting, i == 0 ? "left" : "right");
    cJSON_AddBoolToObject(entry, "supported", foot->supported);
    cJSON_AddBoolToObject(entry, "planted", foot->planted);
    cJSON_AddNumberToObject(entry, "weight", foot->weight);
    cJSON_AddNumberToObject(entry, "error", foot->error);
    cJSON_AddNumberToObject(entry, "speed", foot->speed);
    cJSON_AddNumberToObject(entry, "height", foot->has_reference ? foot->reference[1] - (foot->ground_height + app.animator.foot_lock.floor_offset) : 0);
    cJSON_AddNumberToObject(entry, "ground_height", foot->ground_height);
    cJSON_AddItemToObject(entry, "anchor", cJSON_CreateFloatArray(foot->anchor, 3));
    if (app.model_loaded && foot->supported) {
      cJSON_AddItemToObject(entry, "position", cJSON_CreateFloatArray(app.model.nodes[foot->ankle].world_matrix + 12, 3));
    }
    supported += foot->supported;
  }
  cJSON_AddNumberToObject(planting, "supported", supported);
  cJSON *blend = cJSON_AddObjectToObject(state, "blend");
#ifdef VRM_USE_OZZ
  cJSON_AddBoolToObject(blend, "active", ozz_bridge_blend_active());
  cJSON_AddNumberToObject(blend, "progress", ozz_bridge_blend_ratio());
#else
  cJSON_AddBoolToObject(blend, "active", false);
  cJSON_AddNumberToObject(blend, "progress", 0);
#endif
  cJSON_AddNumberToObject(blend, "duration", g_anim_blend_time_default);
  cJSON_AddNumberToObject(blend, "seam", g_anim_seam_blend_time_default);
  cJSON_AddItemToObject(blend, "groups", cJSON_CreateFloatArray(g_anim_blend_group_factors, BLEND_GROUP_COUNT));
  cJSON *blink = cJSON_AddObjectToObject(state, "blink");
  cJSON_AddBoolToObject(blink, "enabled", app.auto_blink_enabled);
  cJSON_AddNumberToObject(blink, "rate", app.auto_blink_rate);
  cJSON_AddNumberToObject(blink, "duration", app.auto_blink_duration);
  cJSON *camera = cJSON_AddObjectToObject(state, "camera");
  cJSON_AddNumberToObject(camera, "azimuth", app.camera.azimuth);
  cJSON_AddNumberToObject(camera, "polar", app.camera.polar);
  cJSON_AddNumberToObject(camera, "dist", app.camera.distance);
  respond_walk_state(state);
  char *text = cJSON_PrintUnformatted(state);
  ipc_respond_data(&app.ipc, id, text);
  free(text);
  cJSON_Delete(state);
}

static bool valid_number(const cJSON *value, float minimum, float maximum) {
  return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
         value->valuedouble >= minimum && value->valuedouble <= maximum;
}

/* IPC command handler */
static void on_ipc_command(const char *action, void *params, int id,
                           void *user_data) {
  (void)user_data;
  cJSON *json = (cJSON *)params;
  platform_make_context_current(app.window);
  if(walking() && (!strcmp(action,"load") || !strcmp(action,"unload") || !strcmp(action,"load_animation") ||
      !strcmp(action,"pause") || !strcmp(action,"play") || !strcmp(action,"set_time") || !strcmp(action,"set_animation") ||
      !strcmp(action,"blend_to") || !strcmp(action,"set_playback") || !strcmp(action,"set_camera") ||
      !strcmp(action,"set_anim_backend") || !strcmp(action,"set_anim_correction") || !strcmp(action,"reload_animation_metadata") ||
      !strcmp(action,"set_position") || !strcmp(action,"set_rotation") || !strcmp(action,"set_window") ||
      !strcmp(action,"set_auto_resize") || (!strcmp(action,"set_visible") && cJSON_IsFalse(cJSON_GetObjectItem(json,"visible"))) ||
      (!strcmp(action,"set_render") && cJSON_GetObjectItem(json,"scale"))))cancel_walk();

  if (strcmp(action, "ping") == 0) {
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action,"start_walk") == 0) {
    cJSON *token=cJSON_GetObjectItem(json,"motion_id"),*distance=cJSON_GetObjectItem(json,"distance");
    cJSON *target=cJSON_GetObjectItem(json,"x");
    cJSON *restore=cJSON_GetObjectItem(json,"restore_speed");
    if(!cJSON_IsString(token) || !token->valuestring[0] || strlen(token->valuestring)>48 ||
        (target && distance) || (!target && !distance) ||
        (target && !valid_number(target,-131072,131072)) ||
        (distance && (!valid_number(distance,-1600,1600) || fabs(distance->valuedouble)<24)) ||
        (restore && !valid_number(restore,.1,3))) {
      ipc_respond(&app.ipc,id,false,"Expected motion_id and either signed distance (24–1600) or absolute x (-131072–131072)");return;
    }
    if(walking() || !app.visible || !app.model_loaded || app.model.humanoid_bone_count<=0 ||
        app.animator.current_animation<0 || !app.animator.loop_allowed || !app.animator.playing) {
      ipc_respond(&app.ipc,id,false,"Walking requires a visible humanoid and a prepared loopable gait");return;
    }
    double cycle=app.model.animations[app.animator.current_animation].duration;
    if(!isfinite(cycle) || cycle<=0) {ipc_respond(&app.ipc,id,false,"Invalid walking cycle");return;}
    GltfBounds bounds=app.clip_bounds,live;gltf_get_world_bounds(&app.model,&live);gltf_bounds_merge(&bounds,&live);
    float radius=0;
    for(int i=0;i<4;i++)radius=fmaxf(radius,hypotf(bounds.min[0]+(i&1)*(bounds.max[0]-bounds.min[0]),bounds.min[2]+((i>>1)&1)*(bounds.max[2]-bounds.min[2])));
    bounds.min[0]=bounds.min[2]=-radius;bounds.max[0]=bounds.max[2]=radius;app.clip_bounds=bounds;
    bool auto_resize=app.auto_resize;float zoom=app.fit_zoom>0?app.fit_zoom:1;
    app.auto_resize=true;fit_overlay_window(true);app.auto_resize=auto_resize;
    app.fit_zoom=fminf(zoom,app.fit_zoom);
    int w,h;platform_get_window_size(app.window,&w,&h);
    app.walk_goal_w=app.fit_requested_w>0?app.fit_requested_w:w;
    app.walk_goal_h=app.fit_requested_h>0?app.fit_requested_h:h;
    app.camera.viewport_height_scale=app.walk_goal_h/(fmaxf(100,550*app.model_scale)*fmaxf(app.fit_zoom,.01f));
    memset(&app.walk,0,sizeof(app.walk));app.walk.base_yaw=app.model_rotation[1];app.walk.yaw=app.walk.base_yaw;
    snprintf(app.walk_id,sizeof(app.walk_id),"%s",token->valuestring);
    int start_x,start_y;platform_get_window_pos(app.window,&start_x,&start_y);
    app.walk_start_anchor=start_x+w*.5;
    app.walk_absolute=target!=NULL;app.walk_target_x=target?target->valuedouble:0;
    app.walk_requested=target?target->valuedouble-app.walk_start_anchor:distance->valuedouble;
    app.walk_pending_at_ns=(int64_t)(platform_get_time()*1e9);
    app.walk_nominal_speed=fmax(20,avatar_height_pixels()*.45/cycle);
    app.walk_estimate=.4+fmax(.5,fabs(app.walk_requested)/app.walk_nominal_speed+.25);
    app.walk_saved_speed=restore?(float)restore->valuedouble:app.animator.speed;
    app.walk_pending=true;app.walk_started=false;app.animator.speed=0;
    ipc_respond(&app.ipc,id,true,NULL);
  } else if(strcmp(action,"cancel_walk") == 0) {
    cJSON *token=cJSON_GetObjectItem(json,"motion_id");
    if(!cJSON_IsString(token)){ipc_respond(&app.ipc,id,false,"Expected motion_id");return;}
    if(!strcmp(token->valuestring,app.walk_id))cancel_walk();
    ipc_respond(&app.ipc,id,true,NULL);
  } else if (strcmp(action, "get_state") == 0) {
    respond_state(id);
  } else if (strcmp(action, "get_frame_bounds") == 0) {
    respond_frame_bounds(id);
  } else if (strcmp(action, "reset_frame_timing") == 0) {
    app.peak_frame_ms = app.peak_ipc_ms = app.peak_update_ms = 0;
    app.peak_render_ms = app.peak_swap_ms = 0;
    app.slow_frames = app.window_resizes = 0;
    ipc_respond(&app.ipc,id,true,NULL);
  } else if (strcmp(action, "set_foot_lock") == 0) {
    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (!cJSON_IsBool(enabled)) {
      ipc_respond(&app.ipc, id, false, "enabled must be boolean");
      return;
    }
    app.animator.foot_lock.enabled = cJSON_IsTrue(enabled);
    foot_lock_reset(&app.animator.foot_lock);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "save_tuning") == 0) {
    bool saved = save_springs_to_config();
    ipc_respond(&app.ipc, id, saved, "No animation loaded or tuning file could not be saved");
  } else if (strcmp(action, "reload_animation_metadata") == 0) {
    if (!app.has_current_anim) {
      ipc_respond(&app.ipc, id, false, "No animation loaded"); return;
    }
    AnimConfig cfg = {0};
    if (!anim_config_load(app.current_anim_path, &cfg)) {
      ipc_respond(&app.ipc, id, false, "Could not read animation config"); return;
    }
    apply_animation_metadata(&cfg);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_playback") == 0) {
    cJSON *speed = cJSON_GetObjectItem(json, "speed");
    cJSON *loop = cJSON_GetObjectItem(json, "loop");
    if ((speed && !valid_number(speed, 0.1f, 3.0f)) || (loop && !cJSON_IsBool(loop))) {
      ipc_respond(&app.ipc, id, false, "Expected speed in [0.1, 3] and boolean loop");
      return;
    }
    if (cJSON_IsTrue(loop) && app.animator.loop_policy_set && !app.animator.loop_allowed) {
      ipc_respond(&app.ipc, id, false, "Animation is marked loopable=0"); return;
    }
    if (speed) app.animator.speed = (float)speed->valuedouble;
    if (loop) app.animator.loop_remaining = cJSON_IsTrue(loop) ? -1 : 0;
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_blend") == 0) {
    cJSON *duration = cJSON_GetObjectItem(json, "duration");
    cJSON *seam = cJSON_GetObjectItem(json, "seam");
    cJSON *group = cJSON_GetObjectItem(json, "group");
    cJSON *factor = cJSON_GetObjectItem(json, "factor");
    if ((duration && !valid_number(duration, 0.0f, 10.0f)) ||
        (seam && !valid_number(seam, 0.0f, 1.0f)) ||
        ((group || factor) && (!valid_number(group, 0, BLEND_GROUP_COUNT - 1) ||
          group->valuedouble != group->valueint || !valid_number(factor, 0, 2)))) {
      ipc_respond(&app.ipc, id, false, "Invalid blend duration, seam or joint-group factor");
      return;
    }
    if (duration) g_anim_blend_time_default = (float)duration->valuedouble;
    if (seam) g_anim_seam_blend_time_default = (float)seam->valuedouble;
    if (group) g_anim_blend_group_factors[group->valueint] = (float)factor->valuedouble;
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_render") == 0) {
    cJSON *shader = cJSON_GetObjectItem(json, "shader");
    cJSON *top = cJSON_GetObjectItem(json, "always_on_top");
    cJSON *scale = cJSON_GetObjectItem(json, "scale");
    ShaderProgram *program = app.renderer.active;
    RendererShaderMode mode = app.renderer.shader_mode;
    if (shader) {
      if (cJSON_IsString(shader) && strcmp(shader->valuestring, "anime") == 0) {
        program = &app.renderer.anime; mode = RENDERER_SHADER_ANIME;
      } else if (cJSON_IsString(shader) && strcmp(shader->valuestring, "toon") == 0) {
        program = &app.renderer.toon; mode = RENDERER_SHADER_TOON;
      } else if (cJSON_IsString(shader) && strcmp(shader->valuestring, "pbr") == 0) {
        program = &app.renderer.pbr; mode = RENDERER_SHADER_PBR;
      } else { ipc_respond(&app.ipc, id, false, "Unknown shader"); return; }
    }
    if (!program || !program->program || (top && !cJSON_IsBool(top)) ||
        (scale && !valid_number(scale, 0.1f, 10.0f))) {
      ipc_respond(&app.ipc, id, false, "Invalid render options or unavailable shader");
      return;
    }
    app.renderer.active = program;
    app.renderer.shader_mode = mode;
    if (top) {
      set_always_on_top(cJSON_IsTrue(top));
    }
    if (scale) {
      app.model_scale = (float)scale->valuedouble;
      app.camera.distance = 3.0f * app.model_scale;
      app.camera.target[1] = 0.8f * app.model_scale;
      renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation, app.model_scale);
      update_window_for_zoom();
    }
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "clear_expressions") == 0) {
    if (!app.model_loaded) { ipc_respond(&app.ipc, id, false, "No model loaded"); return; }
    char names[MAX_VRM_EXPRESSIONS][VRM_EXPRESSION_NAME_LEN];
    int count = gltf_get_expression_names(&app.model, names, MAX_VRM_EXPRESSIONS);
    for (int i = 0; i < count; i++) gltf_set_expression(&app.model, names[i], 0.0f);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "load") == 0) {
    cJSON *path = cJSON_GetObjectItem(json, "path");
    if (!path || !path->valuestring) {
      ipc_respond(&app.ipc, id, false, "Missing path");
      return;
    }
    cJSON *defer=cJSON_GetObjectItem(json,"defer_show");
    if (defer && !cJSON_IsBool(defer)) {
      ipc_respond(&app.ipc,id,false,"defer_show must be boolean"); return;
    }
    app.presentation_pending=cJSON_IsTrue(defer);
    app.animation_neutral_facing=false;
    hit_capture_reset(&app.hit_capture);
    if (app.model_loaded) {
      gltf_unload(&app.model);
      app.model_loaded = false;
      app.current_model_path[0] = '\0';
      app.has_current_anim = false;
      app.current_anim_path[0] = '\0';
      renderer_set_model(&app.renderer, NULL);
      animator_set_model(&app.animator, NULL);
    }
    if (gltf_load(&app.model, path->valuestring)) {
      memset(app.bounds_cache,0,sizeof(app.bounds_cache));
      app.bounds_cache_cursor = 0;
      app.model_loaded = true;
      app.spring_pose_pending = true;
      snprintf(app.current_model_path, sizeof(app.current_model_path), "%s", path->valuestring);
      app.has_current_anim = false;
      app.current_anim_path[0] = '\0';
      gltf_update_transforms(&app.model);
      spring_bones_init(&app.model);
      renderer_set_model(&app.renderer, &app.model);
      memset(&app.clip_bounds,0,sizeof(app.clip_bounds));
      app.fit_zoom = 1.0f;
      update_window_for_zoom();
      update_anim_correction_mode(app.model.anim_correction_mode);
      animator_set_model(&app.animator, &app.model);
      app.last_time = platform_get_time();
      printf("Loaded: %s (%d animations)\n", path->valuestring,
             app.model.animation_count);
      ipc_respond(&app.ipc, id, true, NULL);
    } else {
      ipc_respond(&app.ipc, id, false, "Failed to load VRM");
    }
  } else if (strcmp(action, "unload") == 0) {
    app.animation_neutral_facing=false;
    hit_capture_reset(&app.hit_capture);
    app.presentation_pending=false;
    if (app.model_loaded) {
      gltf_unload(&app.model);
      app.model_loaded = false;
      app.current_model_path[0] = '\0';
      app.has_current_anim = false;
      app.current_anim_path[0] = '\0';
      renderer_set_model(&app.renderer, NULL);
      animator_set_model(&app.animator, NULL);
    }
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_visible") == 0) {
    cJSON *vis = cJSON_GetObjectItem(json, "visible");
    if (vis) {
      bool was_visible = app.visible;
      app.visible = cJSON_IsTrue(vis);
      if (app.visible != was_visible) {
        frame_clock_reset(&app.frame_clock, platform_get_time());
        app.frame_rate = 0;
      }
      if (app.visible && !was_visible) app.spring_pose_pending = true;
      if (!app.visible) hit_capture_reset(&app.hit_capture);
      if (app.visible)
        platform_show_window(app.window);
      else
        platform_hide_window(app.window);
    }
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_window") == 0) {
    cJSON *x = cJSON_GetObjectItem(json, "x"),
          *y = cJSON_GetObjectItem(json, "y");
    cJSON *w = cJSON_GetObjectItem(json, "width"),
          *h = cJSON_GetObjectItem(json, "height");
    if (x && y)
      platform_move_window(app.window, x->valueint, y->valueint);
    if (w && h)
      platform_resize_window(app.window, w->valueint, h->valueint);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_camera") == 0) {
    cJSON *az = cJSON_GetObjectItem(json, "azimuth"),
          *po = cJSON_GetObjectItem(json, "polar");
    cJSON *di = cJSON_GetObjectItem(json, "dist"),
          *tg = cJSON_GetObjectItem(json, "target");
    if (az)
      app.camera.azimuth = (float)az->valuedouble;
    if (po)
      app.camera.polar = (float)po->valuedouble;
    if (di)
      app.camera.distance = (float)di->valuedouble;
    if (tg && cJSON_IsArray(tg) && cJSON_GetArraySize(tg) >= 3) {
      app.camera.target[0] = (float)cJSON_GetArrayItem(tg, 0)->valuedouble;
      app.camera.target[1] = (float)cJSON_GetArrayItem(tg, 1)->valuedouble;
      app.camera.target[2] = (float)cJSON_GetArrayItem(tg, 2)->valuedouble;
    }
    update_window_for_zoom();
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_auto_resize") == 0) {
    cJSON *en = cJSON_GetObjectItem(json, "enabled");
    app.auto_resize = en ? cJSON_IsTrue(en) : true;
    if (!app.auto_resize) app.camera.viewport_height_scale = 1;
    else if (app.has_current_anim) refit_animation_window();
    update_window_for_zoom();
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "play") == 0) {
    cJSON *speed = cJSON_GetObjectItem(json, "speed");
    animator_play(&app.animator, speed ? (float)speed->valuedouble : 1.0f);
    if (app.model_loaded && app.animator.current_animation>=0)
      app.presentation_pending=false;
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "load_animation") == 0) {
    double load_started = platform_get_time();
    cJSON *path = cJSON_GetObjectItem(json, "path");
    cJSON *neutral = cJSON_GetObjectItem(json, "neutral_facing");
    if (neutral && !cJSON_IsBool(neutral)) {
      ipc_respond(&app.ipc,id,false,"neutral_facing must be boolean");return;
    }
    if (!path || !path->valuestring) {
      ipc_respond(&app.ipc, id, false, "Missing path");
      return;
    }
    if (!app.model_loaded) {
      ipc_respond(&app.ipc, id, false, "No model loaded");
      return;
    }
#ifdef VRM_USE_OZZ
    // Snapshot current pose before loading new animation (for cross-fade
    // blending)
    bool snapshot_taken = false;
    if (animator_get_use_ozz(&app.animator) &&
        app.animator.current_animation >= 0) {
      ozz_bridge_snapshot_pose(&app.model, app.animator.current_animation,
                               app.animator.time);
      snapshot_taken = ozz_bridge_snapshot_available();
    }
#endif
    bool neutral_facing=cJSON_IsTrue(neutral);
    if (neutral_facing ? gltf_load_animation_neutral(&app.model, path->valuestring) : gltf_load_animation(&app.model, path->valuestring)) {
      app.animation_neutral_facing=neutral_facing;
      update_anim_correction_mode(app.model.anim_correction_mode);
      animator_set_model(&app.animator, &app.model);
      app.has_current_anim = false;
      app.current_anim_path[0] = '\0';
      snprintf(app.current_anim_path, sizeof(app.current_anim_path), "%s",
               path->valuestring);
      app.has_current_anim = true;
      g_spring_stiffness = 1.0f;
      g_spring_drag = 1.0f;
      g_spring_gravity = 1.0f;
      g_spring_cloth_mode = 0;
      g_spring_cloth_stiffness = 0.35f;
      g_spring_cloth_drag = 1.2f;
      g_spring_cloth_gravity = 1.0f;
      g_spring_cloth_taper = 0.6f;
      g_anim_seam_blend_time_default = 0.15f;
      AnimConfig cfg = {0};
      if (anim_config_load(app.current_anim_path, &cfg)) {
        if (cfg.has_spring_stiffness)
          g_spring_stiffness = cfg.spring_stiffness;
        if (cfg.has_spring_drag)
          g_spring_drag = cfg.spring_drag;
        if (cfg.has_spring_gravity)
          g_spring_gravity = cfg.spring_gravity;
        if (cfg.has_spring_cloth_mode)
          g_spring_cloth_mode = cfg.spring_cloth_mode >= 0.5f ? 1 : 0;
        if (cfg.has_spring_cloth_stiffness)
          g_spring_cloth_stiffness = cfg.spring_cloth_stiffness;
        if (cfg.has_spring_cloth_drag)
          g_spring_cloth_drag = cfg.spring_cloth_drag;
        if (cfg.has_spring_cloth_gravity)
          g_spring_cloth_gravity = cfg.spring_cloth_gravity;
        if (cfg.has_spring_cloth_taper)
          g_spring_cloth_taper = cfg.spring_cloth_taper;
        if (cfg.has_anim_seam_blend_time)
          g_anim_seam_blend_time_default = cfg.anim_seam_blend_time;
      }
      apply_animation_metadata(&cfg);
      if (app.auto_resize)
        refit_animation_window();
#ifdef VRM_USE_OZZ
      // Prevent one-frame "snap" to the new clip before the client sends
      // blend_to: keep the current node TRS until play/blend commands arrive.
      if (animator_get_use_ozz(&app.animator) && snapshot_taken) {
        animator_pause(&app.animator);
      }

      // If we captured a snapshot prior to load, keep the current pose and let
      // the next blend_to consume the snapshot; sampling the new clip here
      // causes a visible pop that defeats cross-fading.
      if (!(animator_get_use_ozz(&app.animator) &&
            ozz_bridge_snapshot_available())) {
        if (app.model.animation_count > 0) {
          animator_set_animation(&app.animator, 0);
        }
      }
#else
      if (app.model.animation_count > 0) {
        animator_set_animation(&app.animator, 0);
      }
#endif
      app.last_time = platform_get_time();
      app.last_load_ms = (app.last_time-load_started)*1000;
      ipc_respond(&app.ipc, id, true, NULL);
    } else {
      ipc_respond(&app.ipc, id, false, "Failed to load animation");
    }
  } else if (strcmp(action, "pause") == 0) {
    animator_pause(&app.animator);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_time") == 0) {
    cJSON *t = cJSON_GetObjectItem(json, "t");
    if (t)
      animator_set_time(&app.animator, (float)t->valuedouble);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_animation") == 0) {
    cJSON *idx = cJSON_GetObjectItem(json, "index");
    if (idx)
      animator_set_animation(&app.animator, idx->valueint);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_anim_backend") == 0) {
    cJSON *b = cJSON_GetObjectItem(json, "backend");
    if (b && cJSON_IsString(b) && b->valuestring) {
      if (strcmp(b->valuestring, "ozz") == 0) {
        animator_set_use_ozz(&app.animator, true);
      } else if (strcmp(b->valuestring, "legacy") == 0) {
        animator_set_use_ozz(&app.animator, false);
      }
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "backend",
                            animator_get_use_ozz(&app.animator) ? "ozz"
                                                                : "legacy");
    char *json_str = cJSON_PrintUnformatted(result);
    ipc_respond_data(&app.ipc, id, json_str);
    free(json_str);
    cJSON_Delete(result);
  } else if (strcmp(action, "blend_to") == 0) {
    cJSON *idx = cJSON_GetObjectItem(json, "index");
    cJSON *dur = cJSON_GetObjectItem(json, "duration");
    cJSON *loop = cJSON_GetObjectItem(json, "loop"),
          *count = cJSON_GetObjectItem(json, "count");
    int loop_remaining = 0;
    bool loop_specified = false;

    // Parse blend duration (default to auto-compute if not specified or "auto")
    float duration = 0.0f;
    if (dur && cJSON_IsNumber(dur)) {
      duration = (float)dur->valuedouble;
    } else if (dur && cJSON_IsString(dur) &&
               strcmp(dur->valuestring, "auto") == 0) {
      // Will compute auto blend time below
      duration = -1.0f;
    }

    if (count && cJSON_IsNumber(count)) {
      int plays = count->valueint;
      loop_remaining = (plays > 1) ? (plays - 1) : 0;
      loop_specified = true;
    }
    if (loop && cJSON_IsBool(loop)) {
      loop_specified = true;
      loop_remaining = cJSON_IsTrue(loop) ? -1 : 0;
    }
    if (loop_remaining != 0 && app.animator.loop_policy_set && !app.animator.loop_allowed) {
      ipc_respond(&app.ipc, id, false, "Animation is marked loopable=0"); return;
    }

    if (idx) {
      if (!loop_specified) {
        loop_remaining =
            animator_compute_loop_remaining(&app.animator, idx->valueint);
      }
      // Auto blend time if requested
      if (duration < 0.0f) {
        duration =
            animator_compute_auto_blend_time(&app.animator, idx->valueint);
      }
      // Use animator_blend_to for cross-fade (handles ozz blending when
      // enabled)
      animator_blend_to(&app.animator, idx->valueint, duration, loop_remaining);
    }
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_position") == 0) {
    cJSON *x = cJSON_GetObjectItem(json, "x"),
          *y = cJSON_GetObjectItem(json, "y"),
          *z = cJSON_GetObjectItem(json, "z");
    if (x)
      app.model_position[0] = (float)x->valuedouble;
    if (y)
      app.model_position[1] = (float)y->valuedouble;
    if (z)
      app.model_position[2] = (float)z->valuedouble;
    renderer_set_model_transform(&app.renderer, app.model_position,
                                 app.model_rotation, app.model_scale);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_rotation") == 0) {
    cJSON *pitch = cJSON_GetObjectItem(json, "pitch"),
          *yaw = cJSON_GetObjectItem(json, "yaw"),
          *roll = cJSON_GetObjectItem(json, "roll");
    if (pitch)
      app.model_rotation[0] = (float)pitch->valuedouble;
    if (yaw)
      app.model_rotation[1] = (float)yaw->valuedouble;
    if (roll)
      app.model_rotation[2] = (float)roll->valuedouble;
    renderer_set_model_transform(&app.renderer, app.model_position,
                                 app.model_rotation, app.model_scale);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_spring") == 0) {
    struct { const char *key; float *value; float maximum; } fields[] = {
      {"stiffness", &g_spring_stiffness, 5}, {"drag", &g_spring_drag, 3},
      {"gravity", &g_spring_gravity, 5}, {"cloth_stiffness", &g_spring_cloth_stiffness, 3},
      {"cloth_drag", &g_spring_cloth_drag, 3}, {"cloth_gravity", &g_spring_cloth_gravity, 3},
      {"cloth_taper", &g_spring_cloth_taper, 1}
    };
    cJSON *cloth = cJSON_GetObjectItem(json, "cloth_mode");
    cJSON *garment = cJSON_GetObjectItem(json, "garment_collisions");
    if ((cloth && !cJSON_IsBool(cloth)) || (garment && !cJSON_IsBool(garment))) {
      ipc_respond(&app.ipc, id, false, "cloth_mode and garment_collisions must be boolean"); return;
    }
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      cJSON *value = cJSON_GetObjectItem(json, fields[i].key);
      if (value && !valid_number(value, 0, fields[i].maximum)) {
        ipc_respond(&app.ipc, id, false, "Spring parameter is outside its supported range"); return;
      }
    }
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      cJSON *value = cJSON_GetObjectItem(json, fields[i].key);
      if (value) *fields[i].value = (float)value->valuedouble;
    }
    if (cloth) g_spring_cloth_mode = cJSON_IsTrue(cloth);
    if (garment) g_spring_garment_collisions = cJSON_IsTrue(garment);
    if ((cloth || garment) && app.model_loaded) spring_bones_reset(&app.model);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_seam_blend") == 0) {
    cJSON *dur = cJSON_GetObjectItem(json, "duration");
    if (dur && cJSON_IsNumber(dur)) {
      float v = (float)dur->valuedouble;
      if (v < 0.0f)
        v = 0.0f;
      if (v > 1.0f)
        v = 1.0f;
      g_anim_seam_blend_time_default = v;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "seam_blend_time",
                            g_anim_seam_blend_time_default);
    char *json_str = cJSON_PrintUnformatted(result);
    ipc_respond_data(&app.ipc, id, json_str);
    free(json_str);
    cJSON_Delete(result);
  } else if (strcmp(action, "reset_spring") == 0) {
    spring_bones_reset(&app.model);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_anim_correction") == 0) {
    cJSON *m = cJSON_GetObjectItem(json, "mode");
    if (m)
      update_anim_correction_mode(m->valueint);
    rebuild_ozz_after_correction_change(false);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "set_expression") == 0) {
    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *weight = cJSON_GetObjectItem(json, "weight");
    if (!app.model_loaded) {
      ipc_respond(&app.ipc, id, false, "No model loaded");
      return;
    }
    if (!name || !name->valuestring) {
      ipc_respond(&app.ipc, id, false, "Missing name");
      return;
    }
    float w = weight ? (float)weight->valuedouble : 1.0f;
    gltf_set_expression(&app.model, name->valuestring, w);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "list_expressions") == 0) {
    if (!app.model_loaded) {
      ipc_respond(&app.ipc, id, false, "No model loaded");
      return;
    }
    char names[MAX_VRM_EXPRESSIONS][VRM_EXPRESSION_NAME_LEN];
    int count =
        gltf_get_expression_names(&app.model, names, MAX_VRM_EXPRESSIONS);
    cJSON *result = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
      cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
    }
    cJSON_AddItemToObject(result, "expressions", arr);
    char *json_str = cJSON_PrintUnformatted(result);
    ipc_respond_data(&app.ipc, id, json_str);
    free(json_str);
    cJSON_Delete(result);
  } else if (strcmp(action, "shutdown") == 0) {
    ipc_respond(&app.ipc, id, true, NULL);
    app.running = false;
  } else if (strcmp(action, "audio_stream_begin") == 0 || strcmp(action, "audio_cancel") == 0) {
    if (!audio_clear(&app.audio)) {
      ipc_respond(&app.ipc, id, false, "Failed to reset audio device");
      return;
    }
    app.audio_stream_open = strcmp(action, "audio_stream_begin") == 0;
    app.speech_pose_active = false;
    viseme_reset(&app.viseme);
    viseme_set_enabled(&app.viseme, true);
    if (app.model_loaded) {
      const char *vowels[] = {"A", "I", "U", "E", "O"};
      for (int i = 0; i < 5; i++) gltf_set_expression(&app.model, vowels[i], 0.0f);
    }
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "audio_stream_chunk") == 0) {
    cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!cJSON_IsString(data) || !data->valuestring[0]) {
      ipc_respond(&app.ipc, id, false, "Missing base64 PCM data");
      return;
    }
    size_t len = strlen(data->valuestring);
    uint8_t *buf = malloc(len * 3 / 4 + 1);
    if (!buf) { ipc_respond(&app.ipc, id, false, "OOM"); return; }
    size_t decoded_bytes = base64_decode(data->valuestring, len, buf);
    if (decoded_bytes == SIZE_MAX || decoded_bytes % sizeof(int16_t) != 0) {
      free(buf);
      ipc_respond(&app.ipc, id, false, "Expected base64 encoded 16-bit mono PCM");
      return;
    }
    size_t samples = decoded_bytes / sizeof(int16_t);
    int64_t chunk_start = (int64_t)atomic_load(&app.audio.total_written);
    size_t written = audio_write(&app.audio, (int16_t *)buf, samples);
    if (written == samples) viseme_analyze(&app.viseme, (int16_t *)buf, written, chunk_start);
    free(buf);
    ipc_respond(&app.ipc, id, written == samples, "Buffer full; no samples accepted");
  } else if (strcmp(action, "audio_stream_end") == 0) {
    app.audio_stream_open = false;
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "audio_status") == 0) {
    cJSON *result = cJSON_CreateObject();
    size_t available = audio_available(&app.audio);
    size_t free_space = audio_free_space(&app.audio);
    cJSON_AddNumberToObject(result, "sample_rate", AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(result, "channels", AUDIO_CHANNELS);
    cJSON_AddBoolToObject(result, "active", app.audio.active);
    cJSON_AddBoolToObject(result, "stream_open", app.audio_stream_open);
    cJSON_AddBoolToObject(result, "drained", !app.audio_stream_open && available == 0);
    cJSON_AddNumberToObject(result, "available_samples", (double)available);
    cJSON_AddNumberToObject(result, "free_samples", (double)free_space);
    cJSON_AddNumberToObject(result, "total_written", (double)app.audio.total_written);
    cJSON_AddNumberToObject(result, "total_read", (double)app.audio.total_read);
    cJSON_AddNumberToObject(result, "available_seconds",
                            (double)available / (double)AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(result, "free_seconds",
                            (double)free_space / (double)AUDIO_SAMPLE_RATE);
    char *json_str = cJSON_PrintUnformatted(result);
    ipc_respond_data(&app.ipc, id, json_str);
    free(json_str);
    cJSON_Delete(result);
  } else if (strcmp(action, "play_audio_file") == 0) {
    cJSON *path = cJSON_GetObjectItem(json, "path");
    if (path && path->valuestring) {
      if (!audio_clear(&app.audio)) {
        ipc_respond(&app.ipc, id, false, "Failed to reset audio device");
        return;
      }
      app.audio_stream_open = false;
      viseme_reset(&app.viseme);

      // Load and analyze the WAV file
      // We need to load it ourselves to get the PCM data for analysis
      ma_decoder_config config =
          ma_decoder_config_init(ma_format_s16, 1, 24000);
      ma_decoder decoder;

      if (ma_decoder_init_file(path->valuestring, &config, &decoder) ==
          MA_SUCCESS) {
        ma_uint64 length = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &length);

        size_t free_space = audio_free_space(&app.audio);
        if (length > (ma_uint64)free_space) {
          length = free_space;
        }

        int16_t *temp_buf = (int16_t *)malloc(length * sizeof(int16_t));
        if (temp_buf) {
          ma_uint64 frames_read = 0;
          ma_decoder_read_pcm_frames(&decoder, temp_buf, length, &frames_read);

          // Capture start position and analyze for lip sync
          int64_t chunk_start = (int64_t)app.audio.total_written;
          audio_write(&app.audio, temp_buf, (size_t)frames_read);
          viseme_analyze(&app.viseme, temp_buf, (size_t)frames_read,
                         chunk_start);

          free(temp_buf);
          printf("Audio: Loaded %llu samples from %s (with lip sync)\n",
                 (unsigned long long)frames_read, path->valuestring);
          ipc_respond(&app.ipc, id, true, NULL);
        } else {
          ipc_respond(&app.ipc, id, false, "OOM");
        }
        ma_decoder_uninit(&decoder);
      } else {
        ipc_respond(&app.ipc, id, false, "Failed to load WAV");
      }
    } else {
      ipc_respond(&app.ipc, id, false, "Missing path");
    }
  } else if (strcmp(action, "configure_menu") == 0) {
    GemMenu candidate;
    if (!gem_menu_parse(cJSON_GetObjectItemCaseSensitive(json,"menu"),&candidate)) {
      ipc_respond(&app.ipc,id,false,"Invalid or excessive gem menu specification");return;
    }
    app.menu=candidate;
    platform_configure_menu(app.window,&app.menu);
    ipc_respond(&app.ipc,id,true,NULL);
  } else if (strcmp(action, "lip_sync_enable") == 0) {
    cJSON *en = cJSON_GetObjectItem(json, "enabled");
    viseme_set_enabled(&app.viseme, en ? cJSON_IsTrue(en) : true);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "lip_sync_config") == 0) {
    cJSON *gain = cJSON_GetObjectItem(json, "gain");
    cJSON *threshold = cJSON_GetObjectItem(json, "threshold");
    cJSON *smoothing = cJSON_GetObjectItem(json, "smoothing");
    if (gain)
      viseme_set_gain(&app.viseme, (float)gain->valuedouble);
    if (threshold)
      viseme_set_threshold(&app.viseme, (float)threshold->valuedouble);
    if (smoothing)
      viseme_set_smoothing(&app.viseme, (float)smoothing->valuedouble);
    ipc_respond(&app.ipc, id, true, NULL);
  } else if (strcmp(action, "auto_blink_enable") == 0) {
    cJSON *en = cJSON_GetObjectItem(json, "enabled");
    bool enabled = en ? cJSON_IsTrue(en) : true;

    if (enabled && !app.auto_blink_enabled) {
      app.auto_blink_enabled = true;
      auto_blink_start();
      // Return event_id in response
      cJSON *result = cJSON_CreateObject();
      cJSON_AddNumberToObject(result, "event_id", app.auto_blink_event_id);
      char *json_str = cJSON_PrintUnformatted(result);
      ipc_respond_data(&app.ipc, id, json_str);
      free(json_str);
      cJSON_Delete(result);
    } else if (!enabled && app.auto_blink_enabled) {
      app.auto_blink_enabled = false;
      auto_blink_stop();
      ipc_respond(&app.ipc, id, true, NULL);
    } else {
      ipc_respond(&app.ipc, id, true, NULL);
    }
  } else if (strcmp(action, "auto_blink_config") == 0) {
    cJSON *rate = cJSON_GetObjectItem(json, "rate");
    cJSON *duration = cJSON_GetObjectItem(json, "duration");

    if (rate && cJSON_IsNumber(rate)) {
      app.auto_blink_rate = (float)rate->valuedouble;
      if (app.auto_blink_rate < 1.0f)
        app.auto_blink_rate = 1.0f;
      if (app.auto_blink_rate > 60.0f)
        app.auto_blink_rate = 60.0f;
    }
    if (duration && cJSON_IsNumber(duration)) {
      app.auto_blink_duration = (float)duration->valuedouble;
      if (app.auto_blink_duration < 50.0f)
        app.auto_blink_duration = 50.0f;
      if (app.auto_blink_duration > 500.0f)
        app.auto_blink_duration = 500.0f;
    }

    printf("Auto-blink config: rate=%.1f/min, duration=%.0fms\n",
           app.auto_blink_rate, app.auto_blink_duration);
    ipc_respond(&app.ipc, id, true, NULL);
  } else {
    ipc_respond(&app.ipc, id, false, "Unknown action");
  }
}

/* Platform callbacks */
static double drag_start_x, drag_start_y;
static bool is_dragging = false;
static bool is_window_dragging = false;
static int window_drag_start_x = 0, window_drag_start_y = 0;
static int window_drag_start_pos[2] = {0, 0};

static void on_resize(int w, int h, void *user) {
  (void)user;
  (void)w; (void)h;
  /* A wheel burst can deliver several ConfigureNotify events. Reconcile the
   * final size once, immediately before drawing, rather than allocating here. */
}

static void sync_render_size(void) {
  int w,h,ww,wh;
  platform_get_framebuffer_size(app.window,&w,&h);
  platform_get_window_size(app.window,&ww,&wh);
  if(w<=0 || h<=0 || ww<=0 || wh<=0)return;
  if(w!=app.fb_width || h!=app.fb_height ||
     (app.hit_capture.mask.bits &&
      (ww!=app.hit_capture.mask.width || wh!=app.hit_capture.mask.height))) {
    hit_capture_resize(&app.hit_capture,ww,wh);
  }
  if(w!=app.fb_width || h!=app.fb_height) {
    msaa_resize(&app,w,h);
    app.fb_width=w;app.fb_height=h;
  }
  glViewport(0,0,w,h);
  app.camera.aspect=(float)w/h;
  if(app.auto_resize)
    app.camera.viewport_height_scale=wh/((int)fmaxf(100,550*app.model_scale)*fmaxf(app.fit_zoom,0.01f));
}

static void on_mouse_move(double x, double y, void *user) {
  PlatformWindow *win = (PlatformWindow *)user;
  model_mouse_x = (int)x;
  model_mouse_y = (int)y;
  if (is_window_dragging) {
    int wx = 0, wy = 0;
    platform_get_window_pos(win, &wx, &wy);
    int gx = wx + model_mouse_x;
    int gy = wy + model_mouse_y;
    int dx = gx - window_drag_start_x;
    int dy = gy - window_drag_start_y;
    platform_move_window(win, window_drag_start_pos[0] + dx,
                         window_drag_start_pos[1] + dy);
  } else if (is_dragging) {
    if(walking())cancel_walk();
    app.camera.azimuth += (float)(x - drag_start_x) * 0.01f;
    app.camera.polar += (float)(y - drag_start_y) * 0.01f;
    if (app.camera.polar < 0.1f)
      app.camera.polar = 0.1f;
    if (app.camera.polar > 3.0f)
      app.camera.polar = 3.0f;
    drag_start_x = x;
    drag_start_y = y;
  }
}

static void on_mouse_button(int button, int action, void *user) {
  PlatformWindow *win = (PlatformWindow *)user;
  double cx = 0.0, cy = 0.0;
  platform_get_cursor_pos(win, &cx, &cy);
  model_mouse_x = (int)cx;
  model_mouse_y = (int)cy;
  if (button == PLATFORM_MOUSE_LEFT) {
    if (action && model_hit_test(model_mouse_x, model_mouse_y)) {
      if(walking())cancel_walk();
      int wx = 0, wy = 0;
      platform_get_window_pos(win, &wx, &wy);
      window_drag_start_x = wx + model_mouse_x;
      window_drag_start_y = wy + model_mouse_y;
      window_drag_start_pos[0] = wx;
      window_drag_start_pos[1] = wy;
      is_window_dragging = true;
    }
    if (!action) {
      is_window_dragging = false;
    }
  } else if (button == PLATFORM_MOUSE_MIDDLE) {
    is_dragging = action && model_hit_test(model_mouse_x,model_mouse_y);
    if (action) {
      drag_start_x = model_mouse_x;
      drag_start_y = model_mouse_y;
    }
  } else if (button == PLATFORM_MOUSE_RIGHT && action &&
             model_hit_test(model_mouse_x, model_mouse_y)) {
    if (platform_open_menu(win)) return;
    int wx, wy;
    platform_get_window_pos(win,&wx,&wy);
    app.overlay_menu_x = wx+model_mouse_x;
    app.overlay_menu_y = wy+model_mouse_y;
    app.overlay_menu_seq++;
  }
}

static void on_menu_action(int action, void *user) {
  (void)user;
  if(action<0 || action>=app.menu.count)return;
  const char *identity=app.menu.actions[action].id;
  if(app.menu.context[0]) {
    snprintf(app.menu_action_id,sizeof(app.menu_action_id),"%s",identity);
    snprintf(app.menu_action_context,sizeof(app.menu_action_context),"%s",app.menu.context);
    app.menu_action_seq++;
  } else if(!strcmp(identity,"chat"))app.chat_request_seq++;
  else if(!strcmp(identity,"studio"))app.studio_request_seq++;
  else if(!strcmp(identity,"visibility"))app.visibility_request_seq++;
  else if(!strcmp(identity,"quit"))app.running=false;
}

static void on_scroll(double dx, double dy, void *user) {
  (void)user;
  (void)dx;
  if(!isfinite(dy) || dy==0)return;
  if(walking())cancel_walk();
  app.pending_scroll=fmax(-100,fmin(100,app.pending_scroll+dy));
  app.last_scroll_time=platform_get_time();
}

static void apply_scroll_zoom(void) {
  double steps=app.pending_scroll;
  if(steps==0)return;
  app.pending_scroll=0;
  app.model_scale*=(float)(steps>0 ? pow(1.1,steps) : pow(0.9,-steps));
  if (app.model_scale < 0.1f)
    app.model_scale = 0.1f;
  if (app.model_scale > 10.0f)
    app.model_scale = 10.0f;

  /* Scale camera distance and target to keep model in view */
  app.camera.distance = 3.0f * app.model_scale;
  app.camera.target[1] = 0.8f * app.model_scale;

  renderer_set_model_transform(&app.renderer, app.model_position,
                               app.model_rotation, app.model_scale);
  /* The normal frame fit grows the viewport only when needed. Keeping its
   * high-water size avoids compositor reallocations on every zoom-out tick. */
}

static void on_key(int key, int action, void *user) {
  (void)user;
  if (!action)
    return;
  switch (key) {
  case PLATFORM_KEY_ESCAPE:
  case PLATFORM_KEY_Q:
    app.running = false;
    break;
  case PLATFORM_KEY_T:
    set_always_on_top(!app.always_on_top);
    break;
  case PLATFORM_KEY_C:
    renderer_toggle_shader(&app.renderer);
    break;
  case PLATFORM_KEY_B:
    g_spring_cloth_mode = !g_spring_cloth_mode;
    printf("Spring cloth mode %s\n",
           g_spring_cloth_mode ? "enabled" : "disabled");
    if (app.model_loaded)
      spring_bones_reset(&app.model);
    break;
  case PLATFORM_KEY_1:
    update_anim_correction_mode(0);
    rebuild_ozz_after_correction_change(false);
    break;
  case PLATFORM_KEY_2:
    update_anim_correction_mode(1);
    rebuild_ozz_after_correction_change(false);
    break;
  case PLATFORM_KEY_3:
    update_anim_correction_mode(2);
    rebuild_ozz_after_correction_change(false);
    break;
  case PLATFORM_KEY_4:
    update_anim_correction_mode(3);
    rebuild_ozz_after_correction_change(false);
    break;
  case PLATFORM_KEY_5:
    update_anim_correction_mode(4);
    rebuild_ozz_after_correction_change(false);
    break;
  }
}

static void build_view_with_model_transform(float out_view[16]) {
  float *view = (float *)app.camera.view;
  float *model = app.renderer.model_transform;
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += view[k * 4 + row] * model[col * 4 + k];
      }
      out_view[col * 4 + row] = sum;
    }
  }
}

static bool model_hit_test(int x, int y) {
  return app.model_loaded && !app.presentation_pending && app.visible &&
         input_mask_contains(&app.hit_capture.mask,x,y);
}

int main(int argc, char *argv[]) {
  const char *socket_path = NULL;
  char default_socket[108], default_settings[PATH_MAX];
  const char *model_path = NULL;
  const char *window_backend = "auto";
  int exit_status = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: %s [--socket PATH] [--settings PATH] [--window-backend auto|x11|wayland] [MODEL.vrm]\n", argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
      socket_path = argv[++i];
    } else if (strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
      SETTINGS_PATH = argv[++i];
    } else if (strcmp(argv[i], "--window-backend") == 0 && i + 1 < argc) {
      window_backend = argv[++i];
    } else if (strncmp(argv[i], "--window-backend=", 17) == 0) {
      window_backend = argv[i]+17;
    } else if (argv[i][0] != '-' && !model_path) {
      model_path = argv[i];
    } else {
      fprintf(stderr, "Usage: %s [--socket PATH] [--settings PATH] [--window-backend auto|x11|wayland] [MODEL.vrm]\n", argv[0]);
      return 1;
    }
  }
  if (!SETTINGS_PATH) {
    if (!app_settings_path(default_settings, sizeof(default_settings))) {
      fprintf(stderr, "Cannot resolve renderer settings path.\n"); return 1;
    }
    SETTINGS_PATH = default_settings;
    const char *override = getenv("VRM_SETTINGS_PATH");
    if ((!override || !*override) && !app_migrate_settings(SETTINGS_PATH)) {
      fprintf(stderr, "Cannot import legacy renderer settings.\n"); return 1;
    }
  }
  if (!socket_path) {
    if (!app_socket_path(default_socket, sizeof(default_socket))) return 1;
    socket_path = default_socket;
  }
  if (!platform_select_backend(window_backend)) { fprintf(stderr,"Invalid window backend\n"); return 1; }
  if (!ipc_init(&app.ipc, socket_path, on_ipc_command, &app)) return 1;

  app.auto_resize = true;

  app.window = platform_create_window(400, 600, "VRM Overlay", true, false);
  if (!app.window) {
    fprintf(stderr, "Failed to create window\n");
    ipc_cleanup(&app.ipc);
    return 1;
  }

  gem_menu_default(&app.menu);
  platform_configure_menu(app.window,&app.menu);
  platform_make_context_current(app.window);

  printf("OpenGL: %s\n", glGetString(GL_VERSION));
  if (!renderer_init(&app.renderer)) {
    fprintf(stderr, "Failed to init renderer\n");
    platform_destroy_window(app.window);
    ipc_cleanup(&app.ipc);
    return 1;
  }

  GLint max_samples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
  app.msaa_samples = max_samples >= 4 ? 4 : (max_samples > 0 ? max_samples : 1);
  printf("MSAA samples: %d\n", app.msaa_samples);

  int w, h;
  platform_get_framebuffer_size(app.window, &w, &h);
  msaa_resize(&app, w, h);
  app.fb_width = w;
  app.fb_height = h;

  hit_capture_init(&app.hit_capture,app.window);
  app.renderer.capture_hit_mask=true;

  camera_init(&app.camera);
  app.camera.distance = 3.0f;
  app.camera.target[1] = 0.8f;
  app.model_scale = 1.0f;
  {
    Settings settings = {0};
    if (settings_load(SETTINGS_PATH, &settings)) {
      if (settings.has_always_on_top) {
        app.always_on_top = settings.always_on_top;
        platform_set_always_on_top(app.window, app.always_on_top);
      }
      if (settings.has_camera_distance) {
        float saved_distance = settings.camera_distance;
        if (saved_distance < 0.5f)
          saved_distance = 0.5f;
        if (saved_distance > 10.0f)
          saved_distance = 10.0f;
        app.camera.distance = saved_distance;
      }
      if (settings.has_model_scale) {
        float saved_scale = settings.model_scale;
        if (saved_scale < 0.1f)
          saved_scale = 0.1f;
        if (saved_scale > 10.0f)
          saved_scale = 10.0f;
        app.model_scale = saved_scale;
      }
      if (settings.has_model_position) {
        app.model_position[0] = settings.model_position[0];
        app.model_position[1] = settings.model_position[1];
        app.model_position[2] = settings.model_position[2];
      }
      if (!strcmp(platform_backend_name(),"wayland") && settings.has_wayland_position) {
        platform_move_window(app.window,settings.wayland_pos[0],settings.wayland_pos[1]);
      }
      if (strcmp(platform_backend_name(),"wayland") && settings.has_window_position) {
        platform_move_window(app.window, settings.window_pos[0],
                             settings.window_pos[1]);
      }
      if (settings.has_window_size) {
        platform_resize_window(app.window, settings.window_size[0],
                               settings.window_size[1]);
      }
      if (settings.has_model_position || settings.has_model_scale) {
        renderer_set_model_transform(&app.renderer, app.model_position,
                                     app.model_rotation, app.model_scale);
        /* Update camera to match scale */
        app.camera.distance = 3.0f * app.model_scale;
        app.camera.target[1] = 0.8f * app.model_scale;
      }
    }
  }
  app.camera.polar = 1.57f;
  app.camera.azimuth = 3.14f;
  app.camera.aspect = (float)w / (float)h;

  /* App init */
  animator_init(&app.animator);
  audio_init(&app.audio);
  audio_start(&app.audio);
  viseme_init(&app.viseme);
  event_system_init();

  // Initialize auto-blink defaults
  app.auto_blink_enabled = false;
  app.auto_blink_rate = 14.0f;      // 14 blinks per minute
  app.auto_blink_duration = 150.0f; // 150ms blink duration
  app.auto_blink_event_id = 0;
  // Seed random number generator with current time
  g_rand_state = (uint32_t)(platform_get_time() * 1000000.0);

  PlatformCallbacks cb = {on_resize, on_mouse_move, on_mouse_button,
                          on_scroll, on_key, on_menu_action, app.window};
  platform_set_callbacks(app.window, &cb);

  app.visible = true;
  app.running = true;
  app.last_time = platform_get_time();

  frame_clock_reset(&app.frame_clock, app.last_time);

  printf("VRM Overlay started. Press 'q' to quit, 't' for always-on-top\n");
  printf("Press 'c' to toggle cel shading\n");
  printf("Press 'b' to toggle cloth spring mode\n");
  if (model_path) {
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "path", model_path);
    on_ipc_command("load", request, 0, &app);
    cJSON_Delete(request);
    if (!app.model_loaded) {
      fprintf(stderr, "Failed to load startup model: %s\n", model_path);
      app.running = false;
      exit_status = 1;
    }
  }
  printf("IPC socket: %s\n", socket_path);
  fflush(stdout);

  while (app.running && !platform_should_close(app.window)) {
    double frame_started = platform_get_time();
    platform_make_context_current(app.window);
    hit_capture_poll(&app.hit_capture);
    platform_poll_events();
    ipc_poll(&app.ipc);
    if(walking() && app.ipc.client_fd<0)cancel_walk();
    if(app.menu.context[0] && app.ipc.client_fd<0) {
      /* Detaching Studio must not leave a callback-only menu without its owner. */
      gem_menu_default(&app.menu);
      platform_configure_menu(app.window,&app.menu);
    }
    apply_scroll_zoom();
    double ipc_finished = platform_get_time();
    if (app.visible && platform_frame_ready(app.window)) {
      double delay = frame_clock_delay(&app.frame_clock, ipc_finished);
      if (delay > 0) {
        struct timespec wait = {(time_t)delay, (long)((delay - (time_t)delay) * 1e9)};
        while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {}
        double paced = platform_get_time();
        /* Deliberate pacing is not an IPC or rendering stall. */
        frame_started += paced - ipc_finished;
        ipc_finished = paced;
      }
      frame_clock_begin(&app.frame_clock, ipc_finished);
    }

    // Event system updates
    event_drain_commands();
    int64_t now_ns = (int64_t)(platform_get_time() * 1e9);
    int64_t now_samples = (int64_t)app.audio.total_read;
    event_update_seconds(now_ns);
    event_update_audio(now_samples);

    // Process blink events
    event_foreach_active(EVENT_TYPE_BLINK, process_blink, &app);

    double now = platform_get_time();
    float dt = (float)(now - app.last_time);
    app.last_time = now;
    prepare_walk_tick(now_ns);
    if(walk_motion_active(&app.walk)) {
      double previous=app.walk.position;
      walk_motion_update(&app.walk,now_ns);
      app.model_rotation[1]=(float)app.walk.yaw;
      renderer_set_model_transform(&app.renderer,app.model_position,app.model_rotation,app.model_scale);
      int x,y;platform_get_window_pos(app.window,&x,&y);
      int next=(int)lround(app.walk.position);
      if(x!=next)platform_move_window(app.window,next,y);
      if(walk_motion_active(&app.walk))app.animator.speed=dt>0?(float)(fabs(app.walk.position-previous)/(app.walk_nominal_speed*dt)):0;
      else {animator_pause(&app.animator);app.animator.speed=app.walk_saved_speed;}
    }

    animator_update(&app.animator, dt);
    if (app.model_loaded) {
      gltf_update_transforms(&app.model);
      if (app.spring_pose_pending && !app.presentation_pending &&
          app.animator.current_animation>=0) {
        /* An entrance can begin metres from bind pose. Its initial placement
         * is not physical motion and must not kick the skirt/hair sideways. */
        spring_bones_reset(&app.model);
        app.spring_pose_pending=false;
      }
      spring_bones_update(&app.model, dt);
      gltf_update_transforms(&app.model);

      /* Balance against utterance activity, not individual phoneme amplitude,
       * so pauses and packet gaps cannot make the emotion pump up and down. */
      gltf_update_expression_balance(&app.model,
          app.viseme.enabled && (app.audio_stream_open || audio_available(&app.audio) > 0), dt);

      /* Apply lip sync viseme weights to expressions */
      VisemeFrame vf;
      bool has_viseme = false;
      if (app.viseme.enabled && audio_available(&app.audio) > 0) {
        has_viseme = viseme_sample(&app.viseme, (int64_t)app.audio.total_read, &vf);
        if (has_viseme) app.speech_pose_active = true;
      } else if (app.speech_pose_active) {
        /* Release speech's pose once, so idle manual expressions remain usable. */
        memset(&vf, 0, sizeof(vf));
        has_viseme = true;
        app.speech_pose_active = false;
      }
      if (has_viseme) {
        gltf_set_expression(&app.model, "A", vf.weights[VISEME_A]);
        gltf_set_expression(&app.model, "I", vf.weights[VISEME_I]);
        gltf_set_expression(&app.model, "U", vf.weights[VISEME_U]);
        gltf_set_expression(&app.model, "E", vf.weights[VISEME_E]);
        gltf_set_expression(&app.model, "O", vf.weights[VISEME_O]);
      }
    }

    if (!app.visible) {
      struct timespec hidden_wait = {0, 16000000};
      nanosleep(&hidden_wait, NULL); /* Keep IPC responsive without a busy loop. */
      continue;
    }
    if (!platform_frame_ready(app.window)) continue;
    fit_overlay_window(false);
    sync_render_size();
    double update_finished = platform_get_time();

    platform_make_context_current(app.window);
    if (app.msaa_fbo)
      glBindFramebuffer(GL_FRAMEBUFFER, app.msaa_fbo);
    else
      glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Ensure proper GL state for rendering */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glCullFace(GL_BACK);

    camera_update(&app.camera);
    if (!app.presentation_pending) {
      renderer_draw(&app.renderer, &app.camera, app.fb_width, app.fb_height);
    }
    if (app.model_loaded && !app.presentation_pending && app.visible)
      hit_capture_submit(&app.hit_capture,app.msaa_fbo,app.fb_width,app.fb_height,platform_get_time());
    else
      hit_capture_reset(&app.hit_capture);

    platform_present(app.window,app.msaa_fbo,app.fb_width,app.fb_height);

    double render_finished = platform_get_time();
    platform_swap_buffers(app.window);
    double frame_finished = platform_get_time();
    app.frame_rate = frame_clock_present(&app.frame_clock, frame_finished);
    app.frame_ms = (frame_finished-frame_started)*1000;
    if (app.frame_ms > 50) app.slow_frames++;
    if (app.frame_ms > app.peak_frame_ms) {
      app.peak_frame_ms = app.frame_ms;
      app.peak_ipc_ms = (ipc_finished-frame_started)*1000;
      app.peak_update_ms = (update_finished-ipc_finished)*1000;
      app.peak_render_ms = (render_finished-update_finished)*1000;
      app.peak_swap_ms = (frame_finished-render_finished)*1000;
    }

  }

  Settings settings = {0};
  settings_load(SETTINGS_PATH, &settings);
  settings.has_always_on_top = true;
  settings.always_on_top = app.always_on_top;
  settings.has_camera_distance = true;
  settings.camera_distance = app.camera.distance;
  settings.has_model_position = true;
  settings.model_position[0] = app.model_position[0];
  settings.model_position[1] = app.model_position[1];
  settings.model_position[2] = app.model_position[2];
  settings.has_model_scale = true;
  settings.model_scale = app.model_scale;
  {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    platform_get_window_pos(app.window, &wx, &wy);
    platform_get_window_size(app.window, &ww, &wh);
    if (!strcmp(platform_backend_name(),"wayland")) {
      settings.has_wayland_position = true;
      settings.wayland_pos[0] = wx; settings.wayland_pos[1] = wy;
    } else {
      settings.has_window_position = true;
      settings.window_pos[0] = wx; settings.window_pos[1] = wy;
    }
    settings.has_window_size = true;
    settings.window_size[0] = ww;
    settings.window_size[1] = wh;
  }
  settings_save(SETTINGS_PATH, &settings);

  gltf_unload(&app.model);
  hit_capture_cleanup(&app.hit_capture);
  renderer_cleanup(&app.renderer);
  viseme_free(&app.viseme);
  audio_free(&app.audio);
  ipc_cleanup(&app.ipc);
  platform_destroy_window(app.window);
  return exit_status;
}
