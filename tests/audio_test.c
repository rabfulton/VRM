/* Validate complete acceptance, wrapping, sample clocks, and concurrent SPSC IO. */
#include "audio.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

static AudioSystem audio;
static const size_t sample_count = AUDIO_BUFFER_SIZE * 3 + 37;

static void *consume(void *unused) {
    (void)unused;
    int16_t output[127];
    size_t position = 0;
    while (position < sample_count) {
        size_t count = audio_read(&audio, output, 127);
        for (size_t i = 0; i < count; i++) assert(output[i] == (int16_t)((position + i) % 30000));
        position += count;
        if (!count) sched_yield();
    }
    return NULL;
}

int main(void) {
    audio_init(&audio);
    int16_t *samples = calloc(AUDIO_BUFFER_SIZE, sizeof(*samples));
    assert(samples);
    assert(audio_write(&audio, samples, AUDIO_BUFFER_SIZE - 2) == AUDIO_BUFFER_SIZE - 2);
    assert(audio_write(&audio, samples, 2) == 0);
    assert(atomic_load(&audio.total_written) == AUDIO_BUFFER_SIZE - 2);
    assert(audio_free_space(&audio) == 1);
    assert(audio_clear(&audio));
    assert(audio_available(&audio) == 0);
    assert(atomic_load(&audio.total_read) == 0 && atomic_load(&audio.total_written) == 0);
    free(samples);

    pthread_t reader;
    assert(pthread_create(&reader, NULL, consume, NULL) == 0);
    int16_t input[251];
    for (size_t position = 0; position < sample_count;) {
        size_t count = sample_count - position;
        if (count > 251) count = 251;
        for (size_t i = 0; i < count; i++) input[i] = (int16_t)((position + i) % 30000);
        if (audio_write(&audio, input, count)) position += count;
        else sched_yield();
    }
    assert(pthread_join(reader, NULL) == 0);
    assert(audio_available(&audio) == 0);
    assert(atomic_load(&audio.total_written) == sample_count);
    assert(atomic_load(&audio.total_read) == sample_count);
    audio_free(&audio);
    return 0;
}
