#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "DShotRMT.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

static const char *TAG = "dshot-example";


#define DSHOT_RMT_CHANNEL RMT_CHANNEL_0
#define DSHOT_GPIO GPIO_NUM_25

#define MIN_THROTTLE 48
#define FULL_THROTTLE 2047

DShotRMT esc;

static void rampThrottle(int start, int stop, int step)
{
    if (step == 0)
        return;

    for (int i = start; step > 0 ? i < stop : i > stop; i += step)
    {
        esc.sendThrottle(i);
        vTaskDelay(1);
    }
    esc.sendThrottle(stop);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing DShot RMT");
    ESP_ERROR_CHECK(esc.install(DSHOT_GPIO, DSHOT_RMT_CHANNEL));
    ESP_ERROR_CHECK(esc.init());

    ESP_LOGW(TAG, "Reversing");
    ESP_ERROR_CHECK(esc.setReversed(true));

    {
        ESP_LOGI(TAG, "Ramping throttle");
        rampThrottle(MIN_THROTTLE, FULL_THROTTLE, 20);

        ESP_LOGI(TAG, "Full throttle");
        const TickType_t holdStop = xTaskGetTickCount() + (3000 / portTICK_PERIOD_MS);
        while (xTaskGetTickCount() < holdStop)
        {
            esc.sendThrottle(FULL_THROTTLE);
            vTaskDelay(1);
        }

        ESP_LOGI(TAG, "Ramping down");
        rampThrottle(FULL_THROTTLE, MIN_THROTTLE, -20);
    }

    ESP_LOGW(TAG, "Normal direction");
    ESP_ERROR_CHECK(esc.setReversed(false));

    {
        ESP_LOGI(TAG, "Ramping throttle");
        rampThrottle(MIN_THROTTLE, FULL_THROTTLE, 20);

        ESP_LOGI(TAG, "Full throttle");
        const TickType_t holdStop = xTaskGetTickCount() + (3000 / portTICK_PERIOD_MS);
        while (xTaskGetTickCount() < holdStop)
        {
            esc.sendThrottle(FULL_THROTTLE);
            vTaskDelay(1);
        }

        ESP_LOGI(TAG, "Ramping down");
        rampThrottle(FULL_THROTTLE, MIN_THROTTLE, -20);
    }

    ESP_LOGW(TAG, "Exiting");
}
