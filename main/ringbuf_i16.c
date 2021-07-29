#include "ringbuf_i16.h"
#include "assert.h"
#include <stdatomic.h>
#include "esp_log.h"

struct ringbuf_i16_t
{
    int16_t *buffer;
    uint32_t max;
    atomic_uint read;
    atomic_uint write;
};

static uint32_t ringbuf_i16_mask(ringbuf_i16_handle_t rbuf, uint32_t val)
{
    return (val & (rbuf->max - 1));
}

ringbuf_i16_handle_t ringbuf_i16_init(int16_t *buffer, size_t size)
{
    assert(buffer && size);
    assert((size & (size - 1)) == 0);

    ringbuf_i16_handle_t rbuf = malloc(sizeof(ringbuf_i16_t));
    assert(rbuf);

    rbuf->buffer = buffer;
    rbuf->max = size;
    ringbuf_i16_reset(rbuf);

    assert(ringbuf_i16_empty(rbuf));

    return rbuf;
}

void ringbuf_i16_free(ringbuf_i16_handle_t rbuf)
{
    assert(rbuf);
    free(rbuf);
}

void ringbuf_i16_reset(ringbuf_i16_handle_t rbuf)
{
    assert(rbuf);

    atomic_init(&rbuf->read, 0);
    atomic_init(&rbuf->write, 0);
}

void ringbuf_i16_write(ringbuf_i16_handle_t rbuf, int16_t val)
{
    if (ringbuf_i16_full(rbuf))
        atomic_fetch_add(&rbuf->read, 1);

    rbuf->buffer[ringbuf_i16_mask(rbuf, atomic_fetch_add(&rbuf->write, 1))] = val;
}

void ringbuf_i16_write_buf(ringbuf_i16_handle_t rbuf, int16_t *buf, size_t size)
{
    for (int i = 0; i < size; i++)
    {
        ringbuf_i16_write(rbuf, buf[i]);
    }
}

int16_t ringbuf_i16_read(ringbuf_i16_handle_t rbuf)
{
    assert(!ringbuf_i16_empty(rbuf));

    return rbuf->buffer[ringbuf_i16_mask(rbuf, atomic_fetch_add(&rbuf->read, 1))];
}

bool ringbuf_i16_empty(ringbuf_i16_handle_t rbuf)
{
    return atomic_load(&rbuf->read) == atomic_load(&rbuf->write);
}

bool ringbuf_i16_full(ringbuf_i16_handle_t rbuf)
{
    return ringbuf_i16_size(rbuf) == rbuf->max;
}

size_t ringbuf_i16_size(ringbuf_i16_handle_t rbuf)
{
    return atomic_load(&rbuf->write) - atomic_load(&rbuf->read);
}

size_t ringbuf_i16_avail(ringbuf_i16_handle_t rbuf)
{
    return (rbuf->max - ringbuf_i16_size(rbuf));
}