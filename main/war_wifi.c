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
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_config_espnow_rate(WIFI_IF_AP, WIFI_PHY_RATE_11M_S) );
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_NONE) );
    ESP_ERROR_CHECK( esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE) );
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "hidden",
            .ssid_len = 0,
            .channel = 6,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 1,
            .max_connection = 4,
            .beacon_interval = 60000
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_config) );
    //ESP_ERROR_CHECK( esp_wifi_internal_set_fix_rate(WIFI_IF_AP, true, WIFI_PHY_RATE_MCS7_SGI) );
}