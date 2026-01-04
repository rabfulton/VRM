
#define STBI_ONLY_PNG
#include "../../deps/stb/stb_image.h"

#include <epoxy/gl.h>
#include <stdint.h>
#include <stdio.h>

uint8_t *load_image(int *width, int *height, int *num_channels, const char *str){

	stbi_set_flip_vertically_on_load(0);
	uint8_t *data = stbi_load(str, width, height, num_channels, 0);
	return data;	
}

void free_image(uint8_t *data){
	
	if (data != NULL) 
		stbi_image_free(data);
}

void load_texture(const char *str, uint32_t *texture_id, uint16_t mipmaps, uint32_t wrap){
		
	// wrap = GL_CLAMP_TO_EDGE or GL_REPEAT
	// need tocombine the font atlas's and icons into single texture
	// or use texture array!
	int width, height, chn;
	//stbi_set_flip_vertically_on_load(0);
	uint8_t *data = load_image(&width, &height, &chn, str);
	if (!data){
		fprintf(stderr, "ERROR: Could not load image data\n");
		return;
	}
	fprintf(stderr, "INFO: Loaded %s w:%d  h:%d  chn:%d\n", str, width, height, chn);
	glGenTextures(1, texture_id); 
	glBindTexture(GL_TEXTURE_2D, *texture_id);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// default
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// interpolate when upscaling
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);	// default
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, G_NEAREST);	// interpolate when upscaling
	if (mipmaps){
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	// generate mipmaps
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	
	if (chn == 4){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	}
	else if (chn == 3){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	}
	else if (chn == 1){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, data);
	}
	if (mipmaps){
	 	glGenerateMipmap(GL_TEXTURE_2D);
	}
	
	free_image(data);
}
void gl_error_show(){

    GLenum err = GL_NO_ERROR;
    while((err = glGetError()) != GL_NO_ERROR){
        switch (err){
            case GL_NO_ERROR:
            break;
            case GL_INVALID_ENUM:
            fprintf(stderr, "opengl error invalid enum : %i", err); 
            break;            
            case GL_INVALID_VALUE:
            fprintf(stderr, "opengl error invalid value : %i", err);
            break;
            case GL_INVALID_OPERATION:
            fprintf(stderr, "opengl error invalid operation : %i", err);
            break;
            case GL_OUT_OF_MEMORY:
            fprintf(stderr, "opengl error out of memory : %i", err);
            break;
            case GL_STACK_UNDERFLOW:
            fprintf(stderr, "opengl error stack underflow : %i", err);
            break;
            case GL_STACK_OVERFLOW:
            fprintf(stderr, "opengl error stack overflow : %i", err);
            break;
            default:
            fprintf(stderr, "opengl error type unknown : %i", err);
            break;
        }
    }
}


void load_texture_from_mem(void *pixels, int width, int height, int *texture_id, int mipmaps, int wrap, int channels){
		
	glGenTextures(1, texture_id); 
	glBindTexture(GL_TEXTURE_2D, *texture_id);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// default
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// interpolate when upscaling
	if (mipmaps){
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	// generate mipmaps
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	
	if (channels == 4){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}
	else if (channels == 3){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	}
	else if (channels == 1){
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
	}
	if (mipmaps){
	 	glGenerateMipmap(GL_TEXTURE_2D);
	}
	gl_error_show();
}

void gpu_get_texture_size(uint32_t texture_id, int *width, int *height, int channels){

	glBindTexture(GL_TEXTURE_2D, texture_id);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, height);
}

void gpu_download_texture(uint32_t texture_id, void *image_data, uint32_t format){

	glBindTexture(GL_TEXTURE_2D, texture_id);
	glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, image_data);
}

void gpu_delete_texture(uint32_t texture_id){

	glDeleteTextures(1, &texture_id);
}



















