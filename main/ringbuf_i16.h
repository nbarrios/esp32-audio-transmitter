#ifndef __RINGBUF_H__
#define __RINGBUF_H__

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ringbuf_i16_t ringbuf_i16_t;
typedef ringbuf_i16_t* ringbuf_i16_handle_t;

ringbuf_i16_handle_t ringbuf_i16_init(int16_t* buffer, size_t size);

void ringbuf_i16_free(ringbuf_i16_handle_t rbuf);

void ringbuf_i16_reset(ringbuf_i16_handle_t rbuf);

void ringbuf_i16_write(ringbuf_i16_handle_t rbuf, int16_t val);

void ringbuf_i16_write_buf(ringbuf_i16_handle_t rbuf, int16_t* buf, size_t size);

int16_t ringbuf_i16_read(ringbuf_i16_handle_t rbuf);

bool ringbuf_i16_empty(ringbuf_i16_handle_t rbuf);

bool ringbuf_i16_full(ringbuf_i16_handle_t rbuf);

size_t ringbuf_i16_size(ringbuf_i16_handle_t rbuf);

size_t ringbuf_i16_avail(ringbuf_i16_handle_t rbuf);

#ifdef __cplusplus
}
#endif

#endif // __RINGBUF_H__
