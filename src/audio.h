#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Audio format config
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_CHANNELS 1

// Ring buffer size (in samples)
// 24000 samples/sec * 60 seconds = 1,440,000 samples
// Using a power of 2 for efficient wrapping: 2^21 = 2,097,152 (~87 seconds)
#define AUDIO_BUFFER_SIZE (1 << 21)
#define AUDIO_BUFFER_MASK (AUDIO_BUFFER_SIZE - 1)

typedef struct {
  int16_t buffer[AUDIO_BUFFER_SIZE];
  size_t write_pos; // Tail (written to by IPC)
  size_t read_pos;  // Head (read by playback/analysis)
  bool active;

  // Stats
  size_t total_written;
  size_t total_read;

  // Backend (miniaudio device)
  void *backend_data;
} AudioSystem;

// Start audio playback
bool audio_start(AudioSystem *sys);

// Stop audio playback
void audio_stop(AudioSystem *sys);

// Play a wave file from disk (loads entire file into ring buffer)
// Returns true if loaded successfully
bool audio_play_wav(AudioSystem *sys, const char *path);

// Initialize the audio system
void audio_init(AudioSystem *sys);

// Destroy/Cleanup
void audio_free(AudioSystem *sys);

// Push samples into the ring buffer (Thread-safe for single producer)
// Returns number of samples actually written
size_t audio_write(AudioSystem *sys, const int16_t *samples, size_t count);

// Pull samples from the ring buffer (Thread-safe for single consumer)
// Returns number of samples actually read
size_t audio_read(AudioSystem *sys, int16_t *output, size_t count);

// Get number of available samples to read
size_t audio_available(AudioSystem *sys);

// Get number of free slots to write
size_t audio_free_space(AudioSystem *sys);

// Clear the buffer
void audio_clear(AudioSystem *sys);

#endif // AUDIO_H
