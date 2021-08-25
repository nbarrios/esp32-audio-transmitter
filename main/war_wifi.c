#include "war_wifi.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_private/wifi.h"
#include "esp_log.h"

void war_wifi_init() {
    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.ampdu_tx_enable = 0;
    //cfg.nvs_enable = false;
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_12M) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_NONE) );
    ESP_ERROR_CHECK( esp_wifi_set_channel(8, WIFI_SECOND_CHAN_NONE) );
}