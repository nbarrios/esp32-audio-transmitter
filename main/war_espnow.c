#include "war_espnow.h"
#include <string.h>
#include "esp_log.h"
#include "esp_crc.h"

#define ESPNOW_PMK      "8u3NU3cdMdnxmnUN"
#define ESPNOW_LMK      "ZbtUUgbhnfo6WyTQ"
#define ESPNOW_CHANNEL  6
#define ESPNOW_SEND_LEN (48 * 2 * sizeof(int16_t))
#define ESPNOW_MAXDELAY 128

static const char *TAG = "ESP-NOW";

bool is_receiver = false;
RingbufHandle_t espnow_data_rbuf = NULL;

int64_t tx_prev_timer = 0;
uint32_t tx_byte_count = 0;
uint32_t rx_prev_timer = 0;
uint32_t rx_byte_count = 0;

xQueueHandle espnow_queue;
xQueueHandle espnow_data_queue;

uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t receiver_mac[ESP_NOW_ETH_ALEN] = { 0x7c, 0xdf, 0xa1, 0x01, 0x6b, 0x20 };
uint8_t transmitter_mac[ESP_NOW_ETH_ALEN] = { 0x94, 0xb9, 0x7e, 0x89, 0x23, 0x48 };

uint16_t espnow_seq[ESPNOW_DATA_MAX] = {0, 0};

espnow_send_param_t* send_param;

esp_err_t espnow_init(bool receiver) {
    is_receiver = receiver;
    espnow_data_rbuf = NULL;

    espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    espnow_data_queue = xQueueCreate(ESPNOW_DATA_QUEUE_SIZE, ESPNOW_SEND_LEN);
    if (espnow_data_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );

    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t*)ESPNOW_PMK) );

    uint8_t* peer_mac = is_receiver ? transmitter_mac : receiver_mac;
    ESP_LOGI(TAG, "Adding peer: "MACSTR, MAC2STR(peer_mac));
    esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        vSemaphoreDelete(espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = ESPNOW_CHANNEL;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, peer_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    send_param = malloc(sizeof(espnow_send_param_t));
    memset(send_param, 0, sizeof(espnow_send_param_t));
    if (send_param == NULL) {
        vSemaphoreDelete(espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    send_param->unicast = true;
    send_param->broadcast = false;
    send_param->state = 0;
    send_param->magic = receiver ? 0x0000FFFF : 0xFFFF0000;
    send_param->delay = 1000;
    send_param->len = ESPNOW_SEND_LEN + sizeof(espnow_data_t);
    send_param->buffer = malloc(send_param->len);
    if (send_param->buffer == NULL) {
        free(send_param);
        vSemaphoreDelete(espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, peer_mac, ESP_NOW_ETH_ALEN);

    xTaskCreatePinnedToCore(espnow_task, "ESP-Now Task", 2 * 1024, NULL, 3, NULL, 1);

    return ESP_OK;
}

void espnow_deinit(espnow_send_param_t* send_param) {
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(espnow_queue);
    esp_now_deinit();
}

void espnow_set_rbuf(RingbufHandle_t rbuf) {
    espnow_data_rbuf = rbuf;
}

void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    espnow_event_t evt;
    espnow_event_send_cb_t* send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        return;
    }

    evt.id = ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send queue failed.");
    }
}

void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len) {
    espnow_event_t evt;
    espnow_event_recv_cb_t* recv_cb = &evt.info.recv_cb;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        return;
    }

    evt.id = ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data full.");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGI(TAG, "Send receive queue fail.");
        free(recv_cb->data);
    }
}

espnow_data_t* espnow_data_parse(uint8_t* data, uint16_t data_len, uint8_t* state, uint16_t* seq, int* magic) {
    espnow_data_t* buf = (espnow_data_t*) data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len %d", data_len);
        return -1;
    }

    rx_byte_count += data_len;

    if (rx_prev_timer == 0) rx_prev_timer = esp_timer_get_time() / 1000;

    if ((esp_timer_get_time() / 1000) - rx_prev_timer >= 5000) {
        ESP_LOGI(TAG, "Received %0.2f Kbps", ((float)rx_byte_count / 5000.f));
        rx_prev_timer = (esp_timer_get_time() / 1000);
        rx_byte_count = 0;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf;
    }

    return NULL;
}

void espnow_data_prepare(espnow_send_param_t* param) {
    espnow_data_t* buf = (espnow_data_t*) send_param->buffer; 

    assert(send_param->len >= sizeof(espnow_data_t));

    buf->type = ESPNOW_DATA_BROADCAST; //IS_BROADCAST_ADDR(send_param->dest_mac) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;

    xQueueReceive(espnow_data_queue, buf->payload, portMAX_DELAY);

    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

void espnow_task(void *pvParam) {
    if (!is_receiver) espnow_send();
    for (;;) {
        espnow_tick();
    }
    vTaskDelete(NULL);
}

void espnow_tick() {
    espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    uint16_t last_recv_seq = 0;
    uint32_t seq_counter = 0;
    uint32_t missed_seq_count = 0;
    int recv_magic = 0;

    while (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case ESPNOW_SEND_CB:
            {
                espnow_event_send_cb_t* send_cb = &evt.info.send_cb;

                //ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);
                //ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(send_cb->mac_addr));
                if (send_cb->status == ESP_NOW_SEND_FAIL) {
                    ESP_LOGI(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);
                }

                memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                espnow_send();

                break;
            }
            case ESPNOW_RECV_CB:
            {
                espnow_event_recv_cb_t* recv_cb = &evt.info.recv_cb;

                espnow_data_t* data = espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic);
                if (data) {
/*                      ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
                        esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            espnow_deinit(send_param);
                            vTaskDelete(NULL);
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = ESPNOW_CHANNEL;
                        peer->ifidx = ESP_IF_WIFI_STA;
                        peer->encrypt = false;
                        memcpy(peer->lmk, ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    } */

                    if (is_receiver && espnow_data_rbuf != NULL) {
                        if (xRingbufferSend(espnow_data_rbuf, data->payload, ESPNOW_SEND_LEN, portMAX_DELAY) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to send to ringbuffer");
                        }
                    }
                    seq_counter++;
                    if (recv_seq != last_recv_seq + 1) {
                        missed_seq_count++;
                        if (recv_seq < last_recv_seq) {
                            ESP_LOGI(TAG, "Received stale packet: %u", recv_seq);
                        }
                    }
                    if (seq_counter > 5000) {
                        ESP_LOGI(TAG, "Missed %f%% of packets", ((float)missed_seq_count/(float)seq_counter) * 100.f);
                        seq_counter = missed_seq_count = 0;
                    }
                    last_recv_seq = recv_seq;
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                }
                free(recv_cb->data);
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

void espnow_send() {
    espnow_data_prepare(send_param);

    esp_err_t err = esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ESP-Now Send Error: %s", esp_err_to_name(err));
        if (err != ESP_ERR_ESPNOW_NO_MEM) {
            espnow_deinit(send_param);
            vTaskDelete(NULL);
        }
    } else {
        tx_byte_count += send_param->len;
    }


    if (tx_prev_timer == 0) tx_prev_timer = esp_timer_get_time();
    float diff = esp_timer_get_time() - tx_prev_timer;
    if (diff >= 5000000) {
        ESP_LOGI(TAG, "TX Rate: %0.1fKBps", ((float)tx_byte_count * 0.001f) / (diff * 0.000001f));
        tx_prev_timer = esp_timer_get_time();
        tx_byte_count = 0;
    }
}