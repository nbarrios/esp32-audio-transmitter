#ifndef __WAR_ESPNOW_H__
#define __WAR_ESPNOW_H__

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_QUEUE_SIZE           12
#define ESPNOW_DATA_QUEUE_SIZE      5

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

typedef enum {
    ESPNOW_SEND_CB,
    ESPNOW_RECV_CB,
} espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_event_send_cb_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_cb_t;

typedef union {
    espnow_event_send_cb_t send_cb;
    espnow_event_recv_cb_t recv_cb;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

enum {
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_UNICAST,
    ESPNOW_DATA_MAX
};

enum {
    ESPNOW_RBUF_INACTIVE,
    ESPNOW_RBUF_ACTIVE,
    ESPNOW_RBUF_ERROR
};

/* User defined field of ESPNOW data in this example. */
typedef struct {
    uint32_t seq_num;                     //Sequence number of ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint8_t payload[0];                   //Real payload of ESPNOW data.
} __attribute__((packed)) espnow_data_t;

/* Parameters of sending ESPNOW data. */
typedef struct {
    bool unicast;                         //Send unicast ESPNOW data.
    bool broadcast;                       //Send broadcast ESPNOW data.
    uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint16_t count;                       //Total count of unicast ESPNOW data to be sent.
    uint16_t delay;                       //Delay between sending two ESPNOW data, unit: ms.
    int len;                              //Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                      //Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];   //MAC address of destination device.
} espnow_send_param_t;

typedef struct {
    int64_t time;
    int32_t interval;

    int64_t last_micro;
    int64_t micro_accum;
    int32_t micro_count; 

    uint32_t rx_byte_count;
    uint32_t tx_byte_count;

    uint32_t total_packet_count;
    uint32_t missed_packet_count;

    uint32_t ringbuffer_accum;
    uint32_t ringbuffer_count;

    uint16_t receive_queue_accum;
    uint16_t receive_queue_count;
} espnow_debug_t;

extern xQueueHandle espnow_queue;
extern xQueueHandle espnow_data_queue;

esp_err_t espnow_init(bool receiver);
void espnow_deinit(espnow_send_param_t* send_param);
void espnow_set_rbuf(RingbufHandle_t rbuf, size_t len);
void espnow_set_rbuf_state(uint8_t state);
void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);
espnow_data_t* espnow_data_parse(uint8_t* data, uint16_t data_len, uint8_t* state, uint16_t* seq, int* magic);
void espnow_data_prepare(espnow_send_param_t* param);
void espnow_task();
void espnow_tick();
void espnow_send();
void espnow_print_debug();

#ifdef __cplusplus
}
#endif

#endif // __WAR_ESPNOW_H__
