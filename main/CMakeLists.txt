idf_component_register(SRCS 
    "internship.c"
    "LCD_manip.c"
    "LVGL_manip.c"
    "UI_engine.c"
    "touch_manip.c"
    "WIFI_manip.c"
    "VoIP_manip.c"
    "i2c_manip.c"
    "audio_manip.c"
    "camera_manip.c"
    "RTSP_manip.c"
    "h264_manip.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_driver_gpio
        esp_lcd
        esp_timer
        freertos
        log
        esp_lcd_touch
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_hosted
        esp_wifi_remote
        esp_peripherals
        esp_driver_cam
        esp_driver_isp
        esp_driver_ppa
        esp_mm
        esp_driver_jpeg
        esp_h264
        esp_hw_support
        
)

