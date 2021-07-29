#include "vban_client.h"
#include "esp_log.h"
#include "mixer.h"

#define VBAN_TAG "VBAN"
#define VBAN_PORT 6980
#define VBAN_HOST_IP "192.168.1.219"
VBANClient vban_client;
VBANPacket vban_packet;

void vban_client_init()
{
    vban_client.enabled = true;
    vban_client.buffer_threshold = buffer_size / 2;
    vban_client.frame_counter = 0;
    memset(vban_client.tx_buffer, 0, sizeof(vban_client.tx_buffer));

    vban_client.dest_addr.sin_addr.s_addr = inet_addr(VBAN_HOST_IP);
    vban_client.dest_addr.sin_family = AF_INET;
    vban_client.dest_addr.sin_port = htons(VBAN_PORT);

    vban_client.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (vban_client.socket < 0)
    {
        ESP_LOGE(VBAN_TAG, "Unable to create socket: errno %d", errno);
        vban_client.enabled = false;
        return;
    }
    ESP_LOGI(VBAN_TAG, "Socket created, sending to %s:%d", VBAN_HOST_IP, VBAN_PORT);

    vban_packet.fourc = 'NABV';
    vban_packet.sample_rate = 3;
    vban_packet.samples_per_frame = 128;
    vban_packet.channels = 1 - 1;
    vban_packet.data_format = 1;
    memcpy(vban_packet.stream_name, "Guitar", 6);
    vban_packet.frame_counter = 0;

    ringbuf_i16_reset(mixer.ringbuffer);
}

void vban_client_tick()
{
    if (!vban_client.enabled) return;

    while (ringbuf_i16_size(mixer.ringbuffer) >= 256)
    {
        vban_packet.samples_per_frame = 256 - 1;
        vban_packet.frame_counter = vban_client.frame_counter++;
        memcpy(vban_client.tx_buffer, &vban_packet, sizeof(VBANPacket));

        int16_t* audio_data = (int16_t*) (vban_client.tx_buffer + sizeof(VBANPacket));
        for (int i = 0; i < 256; i++)
        {
            audio_data[i] = ringbuf_i16_read(mixer.ringbuffer);
        }

        int err = sendto(
            vban_client.socket,
            vban_client.tx_buffer,
            sizeof(VBANPacket) + 256 * sizeof(int16_t),
            0,
            (struct sockaddr *)&vban_client.dest_addr, 
            sizeof(vban_client.dest_addr)
        );
        if (err < 0)
        {
            ESP_LOGE(VBAN_TAG, "Error occured during sending: errno %d", errno);
            return;
        }
    }
}

void vban_client_deinit()
{
    if (vban_client.socket != -1)
    {
        shutdown(vban_client.socket, 0);
        close(vban_client.socket);
    }
    vban_client.enabled = false;
}