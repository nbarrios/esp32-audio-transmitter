#include "war_espnow.h"

#include <string.h>

#include "esp_crc.h"
#include "esp_log.h"

#define ESPNOW_PMK "8u3NU3cdMdnxmnUN"
#define ESPNOW_LMK "ZbtUUgbhnfo6WyTQ"
#define ESPNOW_CHANNEL 8
#define ESPNOW_SEND_LEN (48 * sizeof(int16_t))
#define ESPNOW_MAXDELAY 128

static const char *TAG = "ESP-NOW";

bool is_receiver = false;
RingbufHandle_t espnow_rbuf = NULL;
size_t espnow_rbuf_len = 0;
uint8_t espnow_data_state = ESPNOW_RBUF_INACTIVE;

xQueueHandle espnow_queue;
xQueueHandle espnow_data_queue;

uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t receiver_mac[ESP_NOW_ETH_ALEN] = {0x7c, 0xdf, 0xa1, 0x01, 0x6b, 0x20};
uint8_t transmitter_mac[ESP_NOW_ETH_ALEN] = {0x94, 0xb9, 0x7e,
                                             0x89, 0x23, 0x48};

uint32_t espnow_seq[ESPNOW_DATA_MAX] = {0, 0};

espnow_send_param_t *send_param;

espnow_debug_t debug = {0};

esp_err_t espnow_init(bool receiver) {
  is_receiver = receiver;

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

  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

  ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));

  uint8_t *peer_mac =
      broadcast_mac;  // is_receiver ? transmitter_mac : receiver_mac;
  ESP_LOGI(TAG, "Adding peer: " MACSTR, MAC2STR(peer_mac));
  esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
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
  ESP_ERROR_CHECK(esp_now_add_peer(peer));
  free(peer);

  send_param = malloc(sizeof(espnow_send_param_t));
  memset(send_param, 0, sizeof(espnow_send_param_t));
  if (send_param == NULL) {
    vSemaphoreDelete(espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }
  send_param->state = 0;
  send_param->resend_scheduled = false;
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

  debug.time = esp_timer_get_time();
  debug.interval = 10 * 1000000;
  debug.last_micro = debug.time;

  xTaskCreatePinnedToCore(espnow_task, "ESP-Now Task", 2 * 1024, NULL, 4, NULL,
                          1);

  return ESP_OK;
}

void espnow_deinit(espnow_send_param_t *send_param) {
  free(send_param->buffer);
  free(send_param);
  vSemaphoreDelete(espnow_queue);
  esp_now_deinit();
}

void espnow_set_rbuf(RingbufHandle_t rbuf, size_t len) {
  espnow_rbuf = rbuf;
  espnow_rbuf_len = len;
}

void espnow_set_rbuf_state(uint8_t state) {
  espnow_data_state = state;
  ESP_LOGI(TAG, "Setting Ringbuffer %s",
           espnow_data_state ? "Active" : "Inactive");
}

void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  espnow_event_t evt;
  espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

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
  espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

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

espnow_data_t *espnow_data_parse(uint8_t *data, uint16_t data_len,
                                 uint8_t *state, uint32_t *seq, int *magic) {
  espnow_data_t *buf = (espnow_data_t *)data;
  uint16_t crc, crc_cal = 0;

  if (data_len < sizeof(espnow_data_t)) {
    ESP_LOGE(TAG, "Receive ESPNOW data too short, len %d", data_len);
    return NULL;
  }

  *seq = buf->seq_num;
  crc = buf->crc;
  buf->crc = 0;
  crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

  if (crc_cal == crc) {
    debug.rx_byte_count += data_len;
    return buf;
  }

  return NULL;
}

void espnow_task(void *pvParam) {
  if (!is_receiver) {
    espnow_data_prepare(send_param);
    espnow_send();
  }
  for (;;) {
    espnow_tick();
  }
  vTaskDelete(NULL);
}

void espnow_tick() {
  espnow_event_t evt;
  uint8_t recv_state = 0;
  uint32_t recv_seq = 0;
  uint32_t last_recv_seq = 0;
  int recv_magic = 0;
  bool repeat_packet = false;

  while (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
    switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

        if (!is_receiver) {
          debug.packet_accum += esp_timer_get_time() - debug.packet_sent;
          debug.packet_count++;
          if (send_param->resend_scheduled) {
            send_param->resend_scheduled = false;
            espnow_send();
          } else {
            memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
            espnow_data_prepare(send_param);
            espnow_send();
          }
        }

        break;
      }
      case ESPNOW_RECV_CB: {
        espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

        debug.total_packet_count++;
        int64_t now = esp_timer_get_time();
        debug.micro_accum += now - debug.last_micro;
        debug.micro_count++;
        debug.last_micro = now;

        espnow_data_t *data =
            espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state,
                              &recv_seq, &recv_magic);
        if (data) {
          if (is_receiver) {
            uint16_t seq_diff = recv_seq - last_recv_seq;
            if (seq_diff == 0) {
              repeat_packet = true;
            } else if (seq_diff != 1 && recv_seq != 0) {
              if (recv_seq < last_recv_seq) {
                ESP_LOGI(TAG, "Received stale packet: %u", recv_seq);
              } else {
                debug.missed_packet_count += seq_diff - 1;
              }
            }
            if (is_receiver && espnow_rbuf != NULL && !repeat_packet) {
              if (espnow_data_state == ESPNOW_RBUF_ACTIVE) {
                if (xRingbufferSend(espnow_rbuf, data->payload, ESPNOW_SEND_LEN,
                                    portMAX_DELAY) != pdTRUE) {
                  ESP_LOGE(TAG, "Failed to send to ringbuffer");
                }
              }
              debug.ringbuffer_accum += xRingbufferGetCurFreeSize(espnow_rbuf);
              debug.ringbuffer_count++;
            }
            last_recv_seq = recv_seq;
            repeat_packet = false;
          }
        } else {
          ESP_LOGI(TAG, "Receive error data from: " MACSTR "",
                   MAC2STR(recv_cb->mac_addr));
        }
        free(recv_cb->data);
        break;
      }
      default:
        ESP_LOGE(TAG, "Callback type error: %d", evt.id);
        break;
    }
    espnow_print_debug();
  }
}

void espnow_data_prepare(espnow_send_param_t *param) {
  espnow_data_t *buf = (espnow_data_t *)send_param->buffer;

  assert(send_param->len >= sizeof(espnow_data_t));

  buf->seq_num = espnow_seq[0]++;
  buf->crc = 0;

  xQueueReceive(espnow_data_queue, buf->payload, portMAX_DELAY);

  buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);

  send_param->resend_scheduled = true;
}

void espnow_send() {
  esp_err_t err =
      esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "ESP-Now Send Error: %s", esp_err_to_name(err));
    if (err != ESP_ERR_ESPNOW_NO_MEM) {
      espnow_deinit(send_param);
      vTaskDelete(NULL);
    }
  } else {
    debug.tx_byte_count += send_param->len;
    debug.packet_sent = esp_timer_get_time();
  }
}

void espnow_print_debug() {
  int64_t now = esp_timer_get_time();
  int64_t diff = now - debug.time;
  if (diff >= debug.interval) {
    debug.time = now;
    float rbuf_bytes_free_avg =
        (float)debug.ringbuffer_accum / debug.ringbuffer_count;
    ESP_LOGI(
        TAG,
        "\nTX: %0.1fKBps, RX: %0.1fKbps\n"
        "Missed %0.2f%%(%u) of packets\n"
        "Audio Ringbuffer Avg: %0.1f%% (%0.1fB Free)\n"
        "RX CB: %0.1f\n"
        "Missed USB Audio CBs: %u\n"
        "Send/CB Delay: %0.1f(%u)",
        ((float)debug.tx_byte_count * 0.001f) / (diff * 0.000001f),
        ((float)debug.rx_byte_count * 0.001f) / (diff * 0.000001f),
        ((float)debug.missed_packet_count / (float)debug.total_packet_count) *
            100.f,
        debug.missed_packet_count,
        (rbuf_bytes_free_avg / (float)espnow_rbuf_len) * 100.f,
        rbuf_bytes_free_avg, (float)debug.micro_accum / debug.micro_count,
        debug.missed_audio_cb, (float)debug.packet_accum / debug.packet_count,
        debug.packet_count);

    debug.rx_byte_count = debug.tx_byte_count = 0;
    debug.total_packet_count = debug.missed_packet_count = 0;
    debug.ringbuffer_accum = debug.ringbuffer_count = 0;
    debug.micro_accum = debug.micro_count = 0;
    debug.missed_audio_cb = 0;
    debug.packet_accum = debug.packet_count = 0;
  }
}