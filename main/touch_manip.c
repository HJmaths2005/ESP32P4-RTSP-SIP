#include "touch_manip.h"




esp_lcd_touch_handle_t touch_init(){

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    io_cfg.scl_speed_hz = 400 * 1000;

    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_cfg, &io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 800,
        .y_max = 1280,
        .rst_gpio_num = GPIO_NUM_22,
        .int_gpio_num = GPIO_NUM_21,
        .levels = {
            .reset     = 0,   // active low
            .interrupt = 0,   // active low
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gsl3680(io, &tp_cfg, &tp));
    return tp;
}