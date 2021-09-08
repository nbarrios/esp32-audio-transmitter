/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "driver/timer.h"
#include "esp_log.h"

//#include "wifi.h"
#include "war_wifi.h"
#include "war_espnow.h"
#include "mixer.h"
#include "vban_client.h"
#include "es8388_i2c.h"

static TaskHandle_t xMainTaskNotify = NULL;

void audio_timer_init();
void main_task(void *pvParam);

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    war_wifi_init();

    ESP_ERROR_CHECK( espnow_init(false) );

    es_i2c_init();
    mixer_init();
    audio_timer_init();

    xTaskCreatePinnedToCore(main_task, "Main Task", 2 * 1024, NULL, 4, NULL, 1);
}

void IRAM_ATTR timer_group0_isr(void *para)
{
    timer_spinlock_take(TIMER_GROUP_0);

    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xMainTaskNotify)
        vTaskNotifyGiveFromISR(xMainTaskNotify, &xHigherPriorityTaskWoken);

    timer_spinlock_give(TIMER_GROUP_0);

    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void audio_timer_init()
{
    ESP_LOGI("Main", "Audio Timer init finished.");
    timer_config_t config;
    config.divider = 16,
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    config.intr_type = TIMER_INTR_LEVEL;

    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);

    const uint64_t alarm_value = (1.0f / 1000.f) * (TIMER_BASE_CLK / 16);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, alarm_value);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr,
        (void*) TIMER_0, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, TIMER_0);
}

void main_task(void *pvParam)
{
    xMainTaskNotify = xTaskGetCurrentTaskHandle();
    uint32_t notification_val;
    for (;;)
    {
        notification_val = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (notification_val == 1)
        {
            mixer_read(); 
        }
    }
}