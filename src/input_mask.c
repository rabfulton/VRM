#include "input_mask.h"
#include <stdlib.h>
#include <string.h>

void input_mask_clear(InputMask *mask) {
  free(mask->bits);
  memset(mask,0,sizeof(*mask));
}

int input_mask_recenter(InputMask *mask, int width, int height) {
  if(!mask->bits || width<=0 || height<=0 || width>16384 || height>16384)return -1;
  if(width==mask->width && height==mask->height)return 0;
  int stride=(width+7)/8;
  unsigned char *bits=calloc((size_t)stride*height,1);
  if(!bits)return -1;
  int dx=(width-mask->width)/2,dy=(height-mask->height)/2;
  for(int y=0;y<mask->height;y++) {
    int target_y=y+dy;
    if(target_y<0 || target_y>=height)continue;
    for(int x=0;x<mask->width;x++) {
      int target_x=x+dx;
      if(target_x>=0 && target_x<width &&
         (mask->bits[(size_t)y*mask->stride+x/8] & (1u<<(x%8))))
        bits[(size_t)target_y*stride+target_x/8]|=(unsigned char)(1u<<(target_x%8));
    }
  }
  free(mask->bits);mask->bits=bits;mask->width=width;mask->height=height;mask->stride=stride;
  return 1;
}

int input_mask_update(InputMask *mask, const unsigned char *stencil,
                      int fw, int fh, int width, int height) {
  if (!stencil || fw<=0 || fh<=0 || width<=0 || height<=0 ||
      fw>16384 || fh>16384 || width>16384 || height>16384) return -1;
  int stride=(width+7)/8;
  size_t size=(size_t)stride*height;
  unsigned char *bits=calloc(size,1);
  if (!bits) return -1;
  int *columns=malloc((size_t)fw*2*sizeof(int));
  if (!columns) { free(bits); return -1; }
  for (int x=0;x<fw;x++) {
    columns[x*2]=x*width/fw;
    columns[x*2+1]=((x+1)*width+fw-1)/fw;
  }
  /* Scan the framebuffer once. Skip background immediately and precompute
   * horizontal scaling, keeping integer division out of the per-pixel loop. */
  for (int sy=0;sy<fh;sy++) {
    int y0=(fh-1-sy)*height/fh, y1=((fh-sy)*height+fh-1)/fh;
    for (int sx=0;sx<fw;sx++) if (stencil[(size_t)sy*fw+sx]) {
      for (int y=y0;y<y1;y++) for (int x=columns[sx*2];x<columns[sx*2+1];x++)
        bits[(size_t)y*stride+x/8] |= (unsigned char)(1u<<(x%8));
    }
  }
  free(columns);
  if (mask->bits && mask->width==width && mask->height==height && memcmp(mask->bits,bits,size)==0) {
    free(bits); return 0;
  }
  input_mask_clear(mask);
  mask->bits=bits; mask->width=width; mask->height=height; mask->stride=stride;
  return 1;
}

bool input_mask_contains(const InputMask *mask, int x, int y) {
  return mask->bits && x>=0 && y>=0 && x<mask->width && y<mask->height &&
         (mask->bits[(size_t)y*mask->stride+x/8] & (1u<<(x%8)))!=0;
}

static bool same_run(const InputMask *mask,int y,int start,int end) {
  if(y<0 || y>=mask->height)return false;
  if(start && input_mask_contains(mask,start-1,y))return false;
  if(end<mask->width && input_mask_contains(mask,end,y))return false;
  for(int x=start;x<end;x++)if(!input_mask_contains(mask,x,y))return false;
  return true;
}

void input_mask_rectangles(const InputMask *mask,
                          void (*emit)(int,int,int,int,void *),void *user) {
  if(!mask || !mask->bits || !emit)return;
  for(int y=0;y<mask->height;y++) for(int x=0;x<mask->width;) {
    if(!input_mask_contains(mask,x,y)) { x++; continue; }
    int start=x++;
    while(x<mask->width && input_mask_contains(mask,x,y))x++;
    if(same_run(mask,y-1,start,x))continue;
    int end=y+1;
    while(same_run(mask,end,start,x))end++;
    emit(start,y,x-start,end-y,user);
  }
}
