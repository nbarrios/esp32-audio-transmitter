#ifndef __MIXER_H__
#define __MIXER_H__

#include "freertos/FreeRTOS.h"
#include "ringbuf_i16.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUFFER_STEREO_SAMPLES 4096
#define BUFFER_SAMPLES (BUFFER_STEREO_SAMPLES * 2)
#define BUFFER_BYTES (BUFFER_SAMPLES * sizeof(int16_t))
#define SAMPLES_TO_BYTES(count) (count * sizeof(int16_t))
#define BYTES_TO_SAMPLES(bytes) (bytes / (sizeof(int16_t)))

struct mixer_buffers_t 
{
    int16_t* mix_buf;
    size_t mix_buf_len;
    int16_t* backing_buffer;
    ringbuf_i16_t* ringbuffer;
};
typedef struct mixer_buffers_t mixer_buffers_t;
extern mixer_buffers_t mixer;

extern const size_t buffer_size;

void mixer_init();
void mixer_tick(size_t samples);
void mixer_read();

#ifdef __cplusplus
}
#endif

#endif // __MIXER_H__
