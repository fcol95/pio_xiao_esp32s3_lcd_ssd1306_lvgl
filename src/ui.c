#include "ui.h"

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

static const char *LOG_TAG = "ui_example";

#define I2C_BUS_PORT        0

#define LCD_PIXEL_CLOCK_HZ  (400 * 1000)
#define LCD_I2C_SDA_PIN_NUM GPIO_NUM_5 // SCL pin for XIAO ESP32S3
#define LCD_I2C_SCL_PIN_NUM GPIO_NUM_6 // SCL pin for XIAO ESP32S3
#define LCD_RESET_PIN_NUM   -1         // No LCD reset pin on XIAO Expansion Base Board
#define LCD_I2C_HW_ADDR     0x3C

// The pixel number in horizontal and vertical
#define SSD1306_LCD_H_RES 128
#define SSD1306_LCD_V_RES 64

// Bit number used to represent command and parameter
#define SSD1306_LCD_CMD_BITS   8
#define SSD1306_LCD_PARAM_BITS 8

static lv_display_t *s_disp = NULL;

static esp_err_t example_lvgl_demo_ui(lv_display_t *disp)
{
    if (disp == NULL)
    {
        ESP_LOGE(LOG_TAG, "Display is NULL");
        return ESP_FAIL;
    }
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, SSD1306_LCD_H_RES);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
    return ESP_OK;
}

esp_err_t ui_init(void)
{
    ESP_LOGI(LOG_TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = LCD_I2C_SDA_PIN_NUM,
        .scl_io_num = LCD_I2C_SCL_PIN_NUM,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_LOGI(LOG_TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t     io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = LCD_I2C_HW_ADDR,
        .scl_speed_hz = LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = SSD1306_LCD_CMD_BITS,   // According to SSD1306 datasheet
        .lcd_param_bits = SSD1306_LCD_CMD_BITS, // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    ESP_LOGI(LOG_TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t     panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = LCD_RESET_PIN_NUM,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = SSD1306_LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(LOG_TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {.io_handle = io_handle,
                                              .panel_handle = panel_handle,
                                              .buffer_size = SSD1306_LCD_H_RES * SSD1306_LCD_V_RES,
                                              .double_buffer = true,
                                              .hres = SSD1306_LCD_H_RES,
                                              .vres = SSD1306_LCD_V_RES,
                                              .monochrome = true,
                                              .rotation = {
                                                  .swap_xy = false,
                                                  .mirror_x = false,
                                                  .mirror_y = false,
                                              }};
    s_disp = lvgl_port_add_disp(&disp_cfg);

    /* Rotation of the screen */
    lv_disp_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);

    return ESP_OK;
}

void ui_task(void *pvParameter)
{
    ESP_LOGI(LOG_TAG, "Display LVGL Scroll Text");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(0))
    {
        example_lvgl_demo_ui(s_disp);
        // Release the mutex
        lvgl_port_unlock();
    }
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}