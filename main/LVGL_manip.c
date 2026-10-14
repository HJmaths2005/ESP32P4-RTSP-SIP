#include "LVGL_manip.h"

extern _lock_t lvgl_api_lock;
lv_indev_t *touch;
extern esp_lcd_touch_handle_t touch_handler;
static const char* TAG = "LVGL_manip";
static uint8_t *s_rotated_pixels;
static size_t s_rotated_capacity;



void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    /* Render widgets without transforms, then rotate each RGB888 strip clockwise.
     * One LVGL buffer keeps this DMA source alive until flush_ready. */
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    const size_t src_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB888);
    assert((size_t)w * h * 3 <= s_rotated_capacity);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *src = px_map + y * src_stride + x * 3;
            uint8_t *dst = s_rotated_pixels + ((size_t)x * h + h - 1 - y) * 3;
            memcpy(dst, src, 3);
        }
    }
    lv_area_t physical = *area;
    lv_display_rotate_area(disp, &physical);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, physical.x1, physical.y1,
                                              physical.x2 + 1, physical.y2 + 1, s_rotated_pixels);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display transfer failed: %s", esp_err_to_name(ret));
        lv_display_flush_ready(disp);
    }
}


void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}


bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

void lvgl_port_task(void *arg)

{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}


void touch_read(lv_indev_t *indev, lv_indev_data_t *indev_data)
{
    /* The GSL3680 driver can return two points even when one is requested. */
    uint16_t x[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    uint16_t y[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    uint16_t strength[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    uint8_t count = 0;
    esp_err_t touch_error = esp_lcd_touch_read_data(touch_handler);
    indev_data->state = LV_INDEV_STATE_RELEASED;
    if (touch_error != ESP_OK) return;
    bool pressed = esp_lcd_touch_get_coordinates(touch_handler, x, y, strength,
                                                &count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    if (pressed && count > 0) {
        indev_data->point.x = x[0];
        indev_data->point.y = y[0];
        indev_data->state = LV_INDEV_STATE_PRESSED;
    }
}
    


void graphical_lib_init(esp_lcd_panel_handle_t lcd_panel, lv_display_t* display){
    ESP_LOGI(TAG, "Initialize LVGL library");
    
    lv_init();
    // create a lvgl display
    display = lv_display_create(EXAMPLE_MIPI_DSI_LCD_H_RES, EXAMPLE_MIPI_DSI_LCD_V_RES);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    // associate the mipi panel handle to the display
    lv_display_set_user_data(display, lcd_panel);
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    // create draw buffer
    void *buf1 = NULL;

    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers");

    size_t alignment = 4;
    size_t draw_buffer_sz = EXAMPLE_MIPI_DSI_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * sizeof(lv_color_t);

    // Keep the display buffer in **internal** RAM can speed up the UI because LVGL uses it a lot and it should have a fast access time
    // This example allocate the buffer from PSRAM mainly because we want to save the internal RAM
    buf1 = heap_caps_aligned_calloc(alignment, 1, draw_buffer_sz, MALLOC_CAP_DEFAULT);
    assert(buf1);
    s_rotated_pixels = heap_caps_aligned_calloc(alignment, 1, draw_buffer_sz, MALLOC_CAP_DEFAULT);
    assert(s_rotated_pixels);
    s_rotated_capacity = draw_buffer_sz;
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, lvgl_flush_cb);
    ESP_LOGI(TAG, "Register DPI panel event callback for LVGL flush ready notification");
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
        .on_refresh_done = /*example_monitor_refresh_rate*/NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(lcd_panel, &cbs, display));

    
    touch = lv_indev_create();
    lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    //adding call back function to touch 
    lv_indev_set_read_cb(touch , touch_read);
    

    ESP_LOGI(TAG, "Use esp_timer as LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));
    xTaskCreate(lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);    
    
}
