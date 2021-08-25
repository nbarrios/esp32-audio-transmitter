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
#include "war_espnow.h"

#define MIXER_TAG "Mixer"

#define SINE_SAMPLES    109
int16_t sine_buffer[SINE_SAMPLES];
uint16_t sine_index = 0;

mixer_buffers_t mixer;

const size_t buffer_ms = 1;
const size_t buffer_samples_per_ms = 48000 / 1000;
const size_t buffer_channels = 2;
const size_t buffer_size =
    buffer_ms * buffer_samples_per_ms;
const size_t stereo_buffer_size =
    buffer_ms * buffer_samples_per_ms * buffer_channels;

uint16_t single_packet_buffer[buffer_ms * buffer_samples_per_ms];

void mixer_init()
{
    //I2S Config
    i2s_config_t i2s_num0_config = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX),
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

    //Sine Wave 440HZ
    double delta = 1.0 / (double)48000;
    double freq = 440.0;
    for (int i = 0; i < SINE_SAMPLES; i++)
    {
        double val = 0.02 * sin(2.0 * M_PI * freq * (double)i * delta);
        sine_buffer[i] = (int16_t) round(val * (double) INT16_MAX);
    }

    mixer.mix_buf = (int16_t*) malloc(stereo_buffer_size * sizeof(int16_t));
    mixer.mix_buf_len = stereo_buffer_size;

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
#define TEST_SINE 1
#if TEST_SINE == 0
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, mixer.mix_buf,
        mixer.mix_buf_len * sizeof(int16_t), &bytes_read, portMAX_DELAY); 
    ESP_ERROR_CHECK(err);

    for (int i = 0, j = 0; i < bytes_read / sizeof(int16_t); i=i+2, j++)
    {
        //Every other sample (mono)
        single_packet_buffer[j] = mixer.mix_buf[i+1];
    }
#else
    for (int i = 0; i < buffer_size; i++) {
        single_packet_buffer[i] = sine_buffer[sine_index];
        sine_index++;
        if (sine_index >= SINE_SAMPLES) sine_index = 0;
    }
#endif
    if (xQueueSend(espnow_data_queue, single_packet_buffer, portMAX_DELAY) != pdTRUE) {
        ESP_LOGI(MIXER_TAG, "Failed to send espnow data.");
    }
}