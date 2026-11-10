#include "st7789_display.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ST7789 bring-up");
    ESP_ERROR_CHECK(display_init());

    display_set_backlight(80);
    display_show_photo();

    ESP_LOGI(TAG, "Photo displayed");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
