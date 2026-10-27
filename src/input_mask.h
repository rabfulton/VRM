#ifndef INPUT_MASK_H
#define INPUT_MASK_H
#include <stdbool.h>

/* Top-left, LSB-first X11 bitmap derived from bottom-left framebuffer samples. */
typedef struct {
  unsigned char *bits;
  int width, height, stride;
} InputMask;

void input_mask_clear(InputMask *mask);
/* Preserve the last visible silhouette around the same optical centre while
 * a resized framebuffer's asynchronous readback is pending. */
int input_mask_recenter(InputMask *mask, int width, int height);
/* Returns 1 on change, 0 if identical, -1 on allocation/size failure. */
int input_mask_update(InputMask *mask, const unsigned char *stencil,
                      int framebuffer_w, int framebuffer_h, int width, int height);
bool input_mask_contains(const InputMask *mask, int x, int y);
#endif
