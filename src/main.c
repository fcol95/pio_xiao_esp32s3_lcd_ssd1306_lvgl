#include <stdio.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h" //< For LED toggling

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"

#include "lcd.h"

static const char *LOG_TAG = "main";

#ifdef CONFIG_BLINK_GPIO
    #define BLINK_GPIO (gpio_num_t) CONFIG_BLINK_GPIO
#else
    #define BLINK_GPIO (gpio_num_t)21 // LED_BUILT_IN
#endif

void blink_task(void *pvParameter)
{
    // Set the GPIO as a push/pull output
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while (1)
    {
        // Blink off (output low)
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Blink on (output high)
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void print_board_info(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t        flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGI(LOG_TAG,
             "  This is %s chip with %d CPU core(s), WiFi%s%s, ",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(LOG_TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Get flash size failed!");
        return;
    }

    ESP_LOGI(LOG_TAG,
             "%ldMB %s flash",
             flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(LOG_TAG, "Minimum free heap size: %ld bytes", esp_get_minimum_free_heap_size());
}

void app_main()
{
    // Set UART log level
    esp_log_level_set(LOG_TAG, ESP_LOG_INFO);

    ESP_LOGI(LOG_TAG, "-- XIAO ESP32S3 LVGL SSD1306 UI Exploration --");

    print_board_info();

    ESP_LOGI(LOG_TAG, "Starting program...");

    // Drivers Init
    esp_err_t lcd_ret = lcd_init();

    // Tasks Init
    xTaskCreate(&blink_task, "blink_task", configMINIMAL_STACK_SIZE, NULL, 5, NULL);
    if (lcd_ret == ESP_OK)
    {
        xTaskCreate(&lcd_task, "lcd_task", configMINIMAL_STACK_SIZE * 4, NULL, 4, NULL);
    }
    else
    {
        ESP_LOGE(LOG_TAG, "UI initialization failed!");
    }
}
