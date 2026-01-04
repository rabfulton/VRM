#ifndef _IMPORT_H_
#define _IMPORT_H_
#include <stdint.h>
void load_texture(const char *str, uint32_t *texture_id, uint16_t mipmaps, uint32_t wrap);
void load_texture_from_mem(void *pixels, int width, int height, int *texture_id, int mipmaps, int wrap, int channels);
void gpu_get_texture_size(uint32_t texture_id, int *width, int *height, int channels);
void gpu_download_texture(uint32_t texture_id, void *image_data, uint32_t format);
void gpu_delete_texture(uint32_t texture_id);
uint8_t *load_image(int *width, int *height, int *num_channels, const char *str);
void free_image(uint8_t *data);
#endif
