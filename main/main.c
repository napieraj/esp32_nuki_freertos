#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "nuki_main";

static void nuki_task(void *pvParameters)
{
    while (1) {
        ESP_LOGI(TAG, "ESP32 Nuki FreeRTOS - running");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Nuki FreeRTOS starting...");

    xTaskCreate(nuki_task, "nuki_task", 4096, NULL, 5, NULL);
}
