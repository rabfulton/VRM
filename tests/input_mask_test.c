#include "input_mask.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char coverage[13*10];
static int rectangles;
static void cover(int x,int y,int w,int h,void *user) {
  (void)user; rectangles++;
  assert(x>=0 && y>=0 && x+w<=13 && y+h<=10 && w>0 && h>0);
  for(int row=y;row<y+h;row++)for(int col=x;col<x+w;col++)
    assert(++coverage[row*13+col]==1);
}
static void check_rectangles(void) {
  /* Ragged, non-byte-aligned masks: emitted regions cover exactly the bits,
   * with no overlap and no filled holes, including vertically split runs. */
  unsigned seed=7;
  for(int sample=0;sample<100;sample++) {
    unsigned char bits[20]={0};
    for(int i=0;i<20;i++) { seed=seed*1664525u+1013904223u; bits[i]=(unsigned char)(seed>>24); }
    InputMask mask={bits,13,10,2};
    memset(coverage,0,sizeof(coverage)); rectangles=0;
    input_mask_rectangles(&mask,cover,NULL);
    for(int y=0;y<10;y++)for(int x=0;x<13;x++)
      assert(coverage[y*13+x]==input_mask_contains(&mask,x,y));
  }
  unsigned char solid[20]; memset(solid,255,sizeof(solid));
  InputMask mask={solid,13,10,2};
  memset(coverage,0,sizeof(coverage)); rectangles=0;
  input_mask_rectangles(&mask,cover,NULL);
  assert(rectangles==1);
}

int main(void) {
  check_rectangles();
  InputMask mask={0};
  /* Two separated pieces with a gap inside their enclosing rectangle. Source
   * data uses OpenGL's bottom-left origin, unlike the X11 input bitmap. */
  unsigned char stencil[8*6]={0};
  stencil[5*8+1]=1; stencil[5*8+6]=1;
  stencil[1*8+4]=1;
  assert(input_mask_update(&mask,stencil,8,6,8,6)==1);
  assert(input_mask_contains(&mask,1,0));
  assert(input_mask_contains(&mask,6,0));
  assert(!input_mask_contains(&mask,3,0));
  assert(input_mask_contains(&mask,4,4));
  assert(!input_mask_contains(&mask,4,1));
  assert(!input_mask_contains(&mask,-1,0));
  assert(!input_mask_contains(&mask,8,0));
  assert(input_mask_update(&mask,stencil,8,6,8,6)==0);
  /* A centred native resize must retain avatar input while the next GPU
   * transfer is pending, preserving holes and non-byte-aligned rows. */
  assert(input_mask_recenter(&mask,13,10)==1);
  assert(input_mask_contains(&mask,3,2));
  assert(input_mask_contains(&mask,8,2));
  assert(input_mask_contains(&mask,6,6));
  assert(!input_mask_contains(&mask,5,2));
  assert(!input_mask_contains(&mask,0,0));
  assert(input_mask_recenter(&mask,13,10)==0);
  assert(input_mask_recenter(&mask,8,6)==1);
  assert(input_mask_contains(&mask,1,0));
  assert(input_mask_contains(&mask,6,0));
  assert(input_mask_contains(&mask,4,4));
  assert(!input_mask_contains(&mask,3,0));
  /* Shrinking crops discarded pixels instead of wrapping them into a row. */
  assert(input_mask_recenter(&mask,4,2)==1);
  for(int y=0;y<2;y++)for(int x=0;x<4;x++)assert(!input_mask_contains(&mask,x,y));
  assert(input_mask_recenter(&mask,0,2)==-1);
  /* At 2x framebuffer density even a single covered subpixel is interactive. */
  memset(stencil,0,sizeof(stencil)); stencil[5*8+3]=1;
  assert(input_mask_update(&mask,stencil,8,6,4,3)==1);
  assert(input_mask_contains(&mask,1,0));
  assert(!input_mask_contains(&mask,0,0));
  /* Fractional scaling, non-byte-aligned rows and empty avatars. */
  assert(input_mask_update(&mask,stencil,8,6,5,4)==1);
  assert(input_mask_contains(&mask,1,0));
  memset(stencil,0,sizeof(stencil));
  assert(input_mask_update(&mask,stencil,8,6,5,4)==1);
  for(int y=0;y<4;y++) for(int x=0;x<5;x++) assert(!input_mask_contains(&mask,x,y));
  assert(input_mask_update(&mask,stencil,0,6,5,4)==-1);
  input_mask_clear(&mask);
  assert(!input_mask_contains(&mask,0,0));
  puts("Input mask passed: holes, origin, centred resizing/cropping, HiDPI/fractional scaling, empty coverage.");
  return 0;
}
