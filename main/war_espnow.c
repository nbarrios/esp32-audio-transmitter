#include "war_espnow.h"
#include <string.h>
#include "esp_log.h"
#include "esp_crc.h"
#include "esp_private/wifi.h"

#define ESPNOW_PMK      "8u3NU3cdMdnxmnUN"
#define ESPNOW_LMK      "ZbtUUgbhnfo6WyTQ"
#define ESPNOW_CHANNEL  1
#define ESPNOW_SEND_LEN (48 * 2 * sizeof(int16_t))
#define ESPNOW_MAXDELAY 512

static const char *TAG = "ESP-NOW";

xQueueHandle espnow_queue;
xQueueHandle espnow_data_queue;

uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint16_t espnow_seq[ESPNOW_DATA_MAX] = {0, 0};

esp_err_t espnow_init(bool receiver) {
    esp_wifi_internal_set_fix_rate(ESP_IF_WIFI_STA, 1, WIFI_PHY_RATE_11M_L);

    espnow_send_param_t* send_param;

    espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    espnow_data_queue = xQueueCreate(3, ESPNOW_SEND_LEN);
    if (espnow_data_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );

    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t*)ESPNOW_PMK) );

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
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    send_param = malloc(sizeof(espnow_send_param_t));
    memset(send_param, 0, sizeof(espnow_send_param_t));
    if (send_param == NULL) {
        vSemaphoreDelete(espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    send_param->unicast = false;
    send_param->broadcast = true;
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
    memcpy(send_param->dest_mac, broadcast_mac, ESP_NOW_ETH_ALEN);
    espnow_data_prepare(send_param);

    xTaskCreate(espnow_task, "espnow_task", 2048, send_param, 4, NULL);

    return ESP_OK;
}

void espnow_deinit(espnow_send_param_t* send_param) {
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(espnow_queue);
    esp_now_deinit();
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
        ESP_LOGW(TAG, "Send receive queue fail.");
        free(recv_cb->data);
    }
}

int espnow_data_parse(uint8_t* data, uint16_t data_len, uint8_t* state, uint16_t* seq, int* magic) {
    espnow_data_t* buf = (espnow_data_t*) data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len %d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}

void espnow_data_prepare(espnow_send_param_t* send_param) {
    espnow_data_t* buf = (espnow_data_t*) send_param->buffer; 

    assert(send_param->len >= sizeof(espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    if (buf->type == ESPNOW_DATA_UNICAST) {
        if (xQueueReceive(espnow_data_queue, buf->payload, portMAX_DELAY) != pdTRUE) {
            ESP_LOGI(TAG, "ESPNOW Data Queue Full");
        }
    } else {
        esp_fill_random(buf->payload, send_param->len - sizeof(espnow_data_t));
    }
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

void espnow_task(void *pvParameter) {
    espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    int recv_magic = 0;
    bool is_broadcast = false;
    int ret;

    vTaskDelay(5000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");

    static uint32_t prev_timer = 0;
    static uint32_t byte_count = 0;

    espnow_send_param_t* send_param = (espnow_send_param_t*) pvParameter;
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        espnow_deinit(send_param);
        vTaskDelete(NULL);
        return;
    }

    while (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case ESPNOW_SEND_CB:
            {
                espnow_event_send_cb_t* send_cb = &evt.info.send_cb;
                is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

                ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                if (is_broadcast && (send_param->broadcast == false)) {
                    break;
                }

                if (is_broadcast && send_param->delay > 0) {
                    vTaskDelay(send_param->delay / portTICK_RATE_MS);
                }
                //ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(send_cb->mac_addr));

                memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                espnow_data_prepare(send_param);

                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    espnow_deinit(send_param);
                    vTaskDelete(NULL);
                }

                byte_count += send_param->len;
                if (prev_timer == 0) prev_timer = esp_timer_get_time() / 1000;
                if ((esp_timer_get_time() / 1000) - prev_timer >= 5000) {
                    ESP_LOGI(TAG, "Sent %0.2f Kbps", ((float)byte_count / 5000.f));
                    prev_timer = (esp_timer_get_time() / 1000);
                    byte_count = 0;
                }

                break;
            }
            case ESPNOW_RECV_CB:
            {
                espnow_event_recv_cb_t* recv_cb = &evt.info.recv_cb;

                ret = espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic);
                free(recv_cb->data);
                if (ret == ESPNOW_DATA_BROADCAST) {
                    ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
                        esp_now_peer_info_t* peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            espnow_deinit(send_param);
                            vTaskDelete(NULL);
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = ESPNOW_CHANNEL;
                        peer->ifidx = ESP_IF_WIFI_STA;
                        peer->encrypt = true;
                        memcpy(peer->lmk, ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    }

                    if (send_param->state == 0) {
                        send_param->state = 1;
                    }

                    if (recv_state == 1) {
                        if (send_param->unicast == false && send_param->magic >= recv_magic) {
                            ESP_LOGI(TAG, "Start sending unicast data");
                    	    ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(recv_cb->mac_addr));

                            memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                            espnow_data_prepare(send_param);
                            if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                                espnow_deinit(send_param);
                                vTaskDelete(NULL);
                            }
                            else {
                                send_param->broadcast = false;
                                send_param->unicast = true;
                            }
                        }
                    }
                }
                else if (ret == ESPNOW_DATA_UNICAST) {
                    //ESP_LOGI(TAG, "Receive %dth unicast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                    send_param->broadcast = false;
                    byte_count += recv_cb->data_len;

                    if (prev_timer == 0) prev_timer = esp_timer_get_time() / 1000;
                    if ((esp_timer_get_time() / 1000) - prev_timer >= 5000) {
                        ESP_LOGI(TAG, "Received %0.2f Kbps", ((float)byte_count / 5000.f));
                        prev_timer = (esp_timer_get_time() / 1000);
                        byte_count = 0;
                    }
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}