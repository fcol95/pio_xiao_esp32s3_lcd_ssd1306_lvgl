#include "lcd.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "lvgl.h"
#include "ui.h" //< For EEZ Studio functions

static const char *LOG_TAG = "lcd";

#define LVGL_LOCK_TIMEOUT_MS 1000U
#define UI_TASK_PERIOD_MS    10U

#define I2C_BUS_PORT         0

#define LCD_PIXEL_CLOCK_HZ   (400 * 1000)
#define LCD_I2C_SDA_PIN_NUM  GPIO_NUM_5 // SDA pin for XIAO ESP32S3 with Grove Base Expansion Board LCD
#define LCD_I2C_SCL_PIN_NUM  GPIO_NUM_6 // SCL pin for XIAO ESP32S3 with Grove Base Expansion Board LCD
#define LCD_RESET_PIN_NUM    -1         // No LCD reset pin on XIAO Expansion Base Board -  -1 for unused
#define LCD_I2C_HW_ADDR      0x3C

// The pixel number in horizontal and vertical
#define SSD1306_LCD_H_RES 128
#define SSD1306_LCD_V_RES 64

// Bit number used to represent command and parameter
#define SSD1306_LCD_CMD_BITS   8
#define SSD1306_LCD_PARAM_BITS 8

static lv_display_t *s_disp = NULL;

// NOTE: This is the old example lvgl demo from espressif before integrating EEZ studio
/*
static esp_err_t example_lvgl_demo_ui(lv_display_t *disp)
{
    ESP_LOGI(LOG_TAG, "Display LVGL Scroll Text");

    if (disp == NULL)
    {
        ESP_LOGE(LOG_TAG, "Display is NULL!");
        return ESP_FAIL;
    }

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (!!!lvgl_port_lock(LVGL_LOCK_TIMEOUT_MS))
    {
        ESP_LOGE(LOG_TAG, "Failed to lock LVGL mutex!");
        return ESP_FAIL;
    }
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    if (scr == NULL)
    {
        ESP_LOGE(LOG_TAG, "Failed to get LVGL active screen!");
        lvgl_port_unlock(); // Release the mutex
        return ESP_FAIL;
    }
    lv_obj_t *label = lv_label_create(scr);
    if (label == NULL)
    {
        ESP_LOGE(LOG_TAG, "Failed to create LVGL label!");

        lvgl_port_unlock(); // Release the mutex
        return ESP_FAIL;
    }

    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, SSD1306_LCD_H_RES);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    lvgl_port_unlock(); // Release the mutex

    return ESP_OK;
}
*/

// NOTE: Getter/Setter for EEZ Studio functions
static int32_t           s_connected_led_state = 0;
static SemaphoreHandle_t s_connected_led_state_mutex = NULL;

int32_t get_var_connected_led_state()
{
    if (s_connected_led_state_mutex == NULL) return 0;
    xSemaphoreTake(s_connected_led_state_mutex, portMAX_DELAY);
    int32_t value = s_connected_led_state;
    xSemaphoreGive(s_connected_led_state_mutex);
    return value;
}
void set_var_connected_led_state(int32_t value)
{
    if (s_connected_led_state_mutex == NULL) return;
    xSemaphoreTake(s_connected_led_state_mutex, portMAX_DELAY);
    s_connected_led_state = value;
    xSemaphoreGive(s_connected_led_state_mutex);
}

// LCD I2C Variables
static i2c_master_bus_handle_t       s_i2c_bus = NULL;
static const i2c_master_bus_config_t s_i2c_bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .i2c_port = I2C_BUS_PORT,
    .sda_io_num = LCD_I2C_SDA_PIN_NUM,
    .scl_io_num = LCD_I2C_SCL_PIN_NUM,
    .flags.enable_internal_pullup = true,
};

static esp_lcd_panel_io_handle_t s_lcd_io_handle = NULL;

static const esp_lcd_panel_io_i2c_config_t io_config = {
    .dev_addr = LCD_I2C_HW_ADDR,
    .scl_speed_hz = LCD_PIXEL_CLOCK_HZ,
    .control_phase_bytes = 1,               // According to SSD1306 datasheet
    .lcd_cmd_bits = SSD1306_LCD_CMD_BITS,   // According to SSD1306 datasheet
    .lcd_param_bits = SSD1306_LCD_CMD_BITS, // According to SSD1306 datasheet
    .dc_bit_offset = 6,                     // According to SSD1306 datasheet
};

static esp_lcd_panel_handle_t         s_lcd_panel_handle = NULL;
static esp_lcd_panel_ssd1306_config_t s_ssd1306_config = {
    .height = SSD1306_LCD_V_RES,
};
static const esp_lcd_panel_dev_config_t s_panel_config = {
    .bits_per_pixel = 1,
    .reset_gpio_num = LCD_RESET_PIN_NUM,
    .vendor_config = &s_ssd1306_config,
};

static const lvgl_port_cfg_t s_lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();

esp_err_t lcd_init(void)
{
    ESP_LOGI(LOG_TAG, "Initialize I2C bus");
    ESP_ERROR_CHECK(i2c_new_master_bus(&s_i2c_bus_config, &s_i2c_bus));
    if (s_i2c_bus == NULL) return ESP_FAIL;

    ESP_LOGI(LOG_TAG, "Install panel IO");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &s_lcd_io_handle));
    if (s_lcd_io_handle == NULL) return ESP_FAIL;

    ESP_LOGI(LOG_TAG, "Install SSD1306 panel driver");

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(s_lcd_io_handle, &s_panel_config, &s_lcd_panel_handle));
    if (s_lcd_panel_handle == NULL) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel_handle, true));

    ESP_LOGI(LOG_TAG, "Initialize LVGL");
    ESP_ERROR_CHECK(lvgl_port_init(&s_lvgl_port_cfg));

    const lvgl_port_display_cfg_t lvgl_port_display_cfg = {.io_handle = s_lcd_io_handle,
                                                           .panel_handle = s_lcd_panel_handle,
                                                           .buffer_size = SSD1306_LCD_H_RES * SSD1306_LCD_V_RES,
                                                           .double_buffer = true,
                                                           .hres = SSD1306_LCD_H_RES,
                                                           .vres = SSD1306_LCD_V_RES,
                                                           .monochrome = true,
                                                            /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
                                                           .rotation = {
                                                               .swap_xy = false,
                                                               .mirror_x = false,
                                                               .mirror_y = false,
                                                           },
                                                           .flags = 
                                                           {
                                                            .sw_rotate = true,
                                                           },
                                                           };
    s_disp = lvgl_port_add_disp(&lvgl_port_display_cfg);
    if (s_disp == NULL) return ESP_FAIL;

    /* Rotation of the screen */
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);

    // Init EEZ UI
    s_connected_led_state_mutex = xSemaphoreCreateMutex();
    if (s_connected_led_state_mutex == NULL) return ESP_FAIL;

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (!!!lvgl_port_lock(LVGL_LOCK_TIMEOUT_MS))
    {
        ESP_LOGE(LOG_TAG, "Failed to lock LVGL mutex!");
        return ESP_FAIL;
    }
    esp_lcd_panel_invert_color(s_lcd_panel_handle,
                               true); // Invert colors to fit with EEZ Studio visual
    ui_init();
    lvgl_port_unlock(); // Release the mutex

    return ESP_OK;
}

void lcd_task(void *pvParameter)
{
    // NOTE: This is the old example lvgl demo from espressif before integrating EEZ studio
    // example_lvgl_demo_ui(s_disp);
    while (1)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (!!!lvgl_port_lock(LVGL_LOCK_TIMEOUT_MS))
        {
            ESP_LOGE(LOG_TAG, "Failed to lock LVGL mutex!");
        }
        else
        {
            ui_tick();
            lvgl_port_unlock(); // Release the mutex
        }

        // NOTE: This is an example of an EEZ Studio simple screen, toggle the variable here shpould toggle the onscreen
        // "LED"
        static TickType_t last_toggle_time = 0;
        TickType_t        current_time = xTaskGetTickCount();
        if ((current_time - last_toggle_time) >= pdMS_TO_TICKS(1000))
        {
            int32_t current_state = get_var_connected_led_state();
            set_var_connected_led_state(!current_state);
            last_toggle_time = current_time;
        }
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
    }
    ESP_ERROR_CHECK(lvgl_port_remove_disp(s_disp));
}
