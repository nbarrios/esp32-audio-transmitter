#include "mixer.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include <string.h>
#include "wm_i2c.h"
#include "math.h"
#include "FilterButterworth24db.h"
#include "Iir.h"
#include <algorithm>

#define MIXER_TAG "Mixer"

mixer_buffers_t mixer;

const size_t buffer_ms = 5;
const size_t buffer_samples_per_ms = 48000 / 1000;
const size_t buffer_channels = 2;
const size_t buffer_size =
    buffer_ms * buffer_samples_per_ms * buffer_channels;

const int order = 2;
Iir::ChebyshevII::LowPass<2> lp1;
Iir::ChebyshevII::LowPass<2> lp2;
Iir::Butterworth::HighPass<order> hp;

void mixer_init()
{
    //I2S Config
    i2s_config_t i2s_num0_config = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = 48000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 1,
        .dma_buf_count = 4,
        .dma_buf_len = 240,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    i2s_driver_install(I2S_NUM_0, &i2s_num0_config, 0, NULL);
    i2s_pin_config_t pin_config = {
        .bck_io_num = 5,
        .ws_io_num = 25,
        .data_out_num = 26,
        .data_in_num = 35
    };
    i2s_set_pin(I2S_NUM_0, &pin_config);

    //MCLK Output
    WRITE_PERI_REG(PIN_CTRL, READ_PERI_REG(PIN_CTRL)&0xFFFFFFF0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);

    mixer.mix_buf = (int16_t*) malloc(buffer_size * sizeof(int16_t));
    mixer.oversampled_buf = (float*) malloc(buffer_size * sizeof(float) * 2);
    mixer.mix_buf_len = buffer_size;

    mixer.backing_buffer = (int16_t*) malloc(1024 * sizeof(int16_t));
    mixer.ringbuffer = ringbuf_i16_init(mixer.backing_buffer, 1024);

    lp1.setup(48000 * 2, 24000, 20);
    lp2.setup(48000 * 2, 24000, 20);
    hp.setup(48000 * 2, 300);

    ESP_LOGI(MIXER_TAG, "Mixer init finished.");
}

void mixer_tick(size_t samples)
{
    static uint32_t total_samples_written = 0;
    static uint32_t time = 0;
    if (time == 0)
    {
        time = esp_timer_get_time() / 1000;
    }

    size_t i2s_bytes_written = 0;
    if (samples > 0)
    {
        i2s_write(I2S_NUM_0, mixer.mix_buf, SAMPLES_TO_BYTES(samples), &i2s_bytes_written, 0);
        if (i2s_bytes_written != SAMPLES_TO_BYTES(samples))
        {
            ESP_LOGI(MIXER_TAG, "I2S Buffer full: %d/%d Bytes written", i2s_bytes_written, SAMPLES_TO_BYTES(samples));
        }
    }
    total_samples_written += BYTES_TO_SAMPLES(i2s_bytes_written);
    uint32_t now = esp_timer_get_time() / 1000;
    uint32_t diff = now - time;
    if (diff > 9999)
    {
        ESP_LOGI(MIXER_TAG, "Samples/ms: %.1f", (float)total_samples_written / (float)diff);
        total_samples_written = 0;
        time = now;
    }
}

void mixer_read()
{
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, mixer.mix_buf,
        mixer.mix_buf_len * sizeof(int16_t), &bytes_read, portMAX_DELAY); 
    ESP_ERROR_CHECK(err);

    for (int i = 0; i < bytes_read / sizeof(int16_t); i=i+2)
    {
        //Every other sample (mono)
        ringbuf_i16_write(mixer.ringbuffer, mixer.mix_buf[i]);
    }

/*     uint32_t samples_read = bytes_read / sizeof(int16_t);
    uint32_t j = 0;
    for (uint32_t i = 0; i < samples_read; i = i + 2, j++)
    {
        float sam = (float) mixer.mix_buf[i] / (float) INT16_MAX;
        mixer.oversampled_buf[j << 1] = sam;
        mixer.oversampled_buf[(j << 1) + 1] = 0.f;
    }
    mixer.oversampled_len = samples_read;

    for (uint32_t i = 0; i < mixer.oversampled_len; i++)
    {
        float sam = lp1.filter(mixer.oversampled_buf[i]);
        //sam = (fabs(sam) < 0.0001f) ? 0.0f : sam; //Noise gate
        const float gain = 1.5f;

        //sam = hp.filter(sam);

        //sam = 2.5f * sam - 3.05f * sam * sam * sam;
        if (sam < 0)
        {
            sam = -1.0f * tanh(gain * sam);
            sam = -1.0f * tanh(gain * sam);
        }
        sam = std::max(sam, -1.0f);
        sam = std::min(sam, 1.0f);

        mixer.oversampled_buf[i] = lp2.filter(sam);
    }

    j = 0;
    for (uint32_t i = 0; i < samples_read; i = i + 2, j++)
    {
        mixer.mix_buf[i] = mixer.oversampled_buf[j << 1] * INT16_MAX;
        mixer.mix_buf[i+1] = mixer.mix_buf[i];
    } */

    err = i2s_write(I2S_NUM_0, mixer.mix_buf, bytes_read, &bytes_written, portMAX_DELAY); 
    ESP_ERROR_CHECK(err);
}