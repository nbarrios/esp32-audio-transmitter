#ifndef __VBAN_CLIENT_H__
#define __VBAN_CLIENT_H__

#include <stdint.h>
#include "esp_netif.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#pragma pack(push, 1)
typedef struct VBANPacket_t
{
    uint32_t fourc;
    uint8_t sample_rate;
    uint8_t samples_per_frame;
    uint8_t channels;
    uint8_t data_format;
    char stream_name[16];
    uint32_t frame_counter;

} VBANPacket;
#pragma pack(pop)

typedef struct VBANClient_t
{
    bool enabled;
    uint32_t buffer_threshold;
    uint32_t frame_counter;
    struct sockaddr_in dest_addr;
    int socket;
    fd_set write_set;
    fd_set read_set;
    uint8_t tx_buffer[1500];
} VBANClient;
extern VBANClient vban_client;

void vban_client_init();
void vban_client_tick();
void vban_client_deinit();

#endif // __VBAN_CLIENT_H__
