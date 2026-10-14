#include "common.h"
#include "lvgl.h"

#define EXAMPLE_LVGL_DRAW_BUF_LINES    (10) // number of display lines in each draw buffer
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     1
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define EXAMPLE_PIN_NUM_REFRESH_MONITOR         20 







void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void lvgl_port_task(void *arg);
bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx);
void touch_read(lv_indev_t *indev, lv_indev_data_t *indev_data);
void graphical_lib_init(esp_lcd_panel_handle_t lcd_panel, lv_display_t* display);





