#define MINIAUDIO_IMPLEMENTATION
#include "audio.h"
#include "../deps/miniaudio.h"
#include <stdio.h>
#include <string.h>

// Miniaudio data callback
static void data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount) {
  AudioSystem *sys = (AudioSystem *)pDevice->pUserData;
  if (!sys)
    return;

  // Output format is configured as int16
  int16_t *out = (int16_t *)pOutput;

  // Read from ring buffer
  size_t read = audio_read(sys, out, frameCount);

  // Fill remainder with silence if buffer underrun
  if (read < frameCount) {
    memset(out + read, 0, (frameCount - read) * sizeof(int16_t));
  }
}

void audio_init(AudioSystem *sys) {
  if (!sys)
    return;
  memset(sys->buffer, 0, sizeof(sys->buffer));
  sys->write_pos = 0;
  sys->read_pos = 0;
  sys->total_written = 0;
  sys->total_read = 0;
  sys->active = true;
  sys->backend_data = NULL;
  printf("Audio: Initialized ring buffer size=%d samples\n", AUDIO_BUFFER_SIZE);
}

bool audio_start(AudioSystem *sys) {
  if (!sys)
    return false;

  // Check if already running
  if (sys->backend_data)
    return true;

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_s16;
  config.playback.channels = AUDIO_CHANNELS;
  config.sampleRate = AUDIO_SAMPLE_RATE;
  config.dataCallback = data_callback;
  config.pUserData = sys;

  ma_device *device = (ma_device *)malloc(sizeof(ma_device));
  if (ma_device_init(NULL, &config, device) != MA_SUCCESS) {
    printf("Audio Error: Failed to initialize playback device.\n");
    free(device);
    return false;
  }

  if (ma_device_start(device) != MA_SUCCESS) {
    printf("Audio Error: Failed to start playback device.\n");
    ma_device_uninit(device);
    free(device);
    return false;
  }

  sys->backend_data = device;
  printf("Audio: Playback started at %d Hz\n", AUDIO_SAMPLE_RATE);
  return true;
}

void audio_stop(AudioSystem *sys) {
  if (!sys || !sys->backend_data)
    return;

  ma_device *device = (ma_device *)sys->backend_data;
  ma_device_uninit(device);
  free(device);
  sys->backend_data = NULL;
  printf("Audio: Playback stopped\n");
}

void audio_free(AudioSystem *sys) {
  if (!sys)
    return;
  audio_stop(sys);
  sys->active = false;
}

size_t audio_available(AudioSystem *sys) {
  if (!sys)
    return 0;
  // Load local copies to avoid tearing if checked from other thread
  size_t w = sys->write_pos;
  size_t r = sys->read_pos;

  if (w >= r) {
    return w - r;
  } else {
    return (AUDIO_BUFFER_SIZE - r) + w;
  }
}

size_t audio_free_space(AudioSystem *sys) {
  if (!sys)
    return 0;
  return AUDIO_BUFFER_SIZE - audio_available(sys) - 1; // Keep 1 slot empty
}

size_t audio_write(AudioSystem *sys, const int16_t *samples, size_t count) {
  if (!sys || !samples || count == 0)
    return 0;

  size_t free_space = audio_free_space(sys);
  if (count > free_space) {
    count = free_space; // Drop excess or handle partial write
  }

  size_t w = sys->write_pos;
  size_t first_chunk = AUDIO_BUFFER_SIZE - w;

  if (count <= first_chunk) {
    // Contiguous write
    memcpy(&sys->buffer[w], samples, count * sizeof(int16_t));
    sys->write_pos = (w + count) & AUDIO_BUFFER_MASK;
  } else {
    // Wrap around
    memcpy(&sys->buffer[w], samples, first_chunk * sizeof(int16_t));
    memcpy(&sys->buffer[0], &samples[first_chunk],
           (count - first_chunk) * sizeof(int16_t));
    sys->write_pos = (count - first_chunk) & AUDIO_BUFFER_MASK;
  }

  sys->total_written += count;
  return count;
}

size_t audio_read(AudioSystem *sys, int16_t *output, size_t count) {
  if (!sys || !output || count == 0)
    return 0;

  size_t available = audio_available(sys);
  if (count > available) {
    count = available;
  }

  size_t r = sys->read_pos;
  size_t first_chunk = AUDIO_BUFFER_SIZE - r;

  if (count <= first_chunk) {
    // Contiguous read
    memcpy(output, &sys->buffer[r], count * sizeof(int16_t));
    sys->read_pos = (r + count) & AUDIO_BUFFER_MASK;
  } else {
    // Wrap around
    memcpy(output, &sys->buffer[r], first_chunk * sizeof(int16_t));
    memcpy(&output[first_chunk], &sys->buffer[0],
           (count - first_chunk) * sizeof(int16_t));
    sys->read_pos = (count - first_chunk) & AUDIO_BUFFER_MASK;
  }

  sys->total_read += count;
  return count;
}

void audio_clear(AudioSystem *sys) {
  if (!sys)
    return;
  sys->read_pos = sys->write_pos; // Reset head to tail
  sys->total_written = 0;
  sys->total_read = 0;
}

bool audio_play_wav(AudioSystem *sys, const char *path) {
  if (!sys || !path)
    return false;

  // Initialize config for 16-bit, mono, correct sample rate
  ma_decoder_config config =
      ma_decoder_config_init(ma_format_s16, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);
  ma_decoder decoder;

  if (ma_decoder_init_file(path, &config, &decoder) != MA_SUCCESS) {
    printf("Audio Error: Failed to load WAV file: %s\n", path);
    return false;
  }

  // Determine length
  ma_uint64 length = 0;
  ma_decoder_get_length_in_pcm_frames(&decoder, &length);

  // Check if it fits (basic check, we'll write what we can)
  size_t free_space = audio_free_space(sys);
  if (length > (ma_uint64)free_space) {
    printf("Audio Warning: WAV file too large for buffer (%llu > %zu), "
           "truncating.\n",
           (unsigned long long)length, free_space);
    length = free_space;
  }

  // Allocate temp buffer
  int16_t *temp_buf =
      (int16_t *)malloc(length * AUDIO_CHANNELS * sizeof(int16_t));
  if (!temp_buf) {
    ma_decoder_uninit(&decoder);
    return false;
  }

  // Read frames (miniaudio handles resampling/format conversion)
  ma_uint64 frames_read = 0;
  ma_decoder_read_pcm_frames(&decoder, temp_buf, length, &frames_read);

  // Write to ring buffer
  audio_write(sys, temp_buf, (size_t)frames_read);

  free(temp_buf);
  ma_decoder_uninit(&decoder);

  printf("Audio: Loaded %llu samples from %s\n",
         (unsigned long long)frames_read, path);
  return true;
}
