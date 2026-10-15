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
    "rtsp_sdp_compat.c"
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
        espressif__esp_media_protocols
        esp_driver_cam
        esp_driver_isp
        esp_driver_ppa
        esp_mm
        esp_driver_jpeg
        esp_h264
        esp_hw_support
        
)



# if(CONFIG_APP_USE_H264)
# Compatibility fix for FU-A SDP in the prebuilt ADF RTSP server.
#   target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=media_lib_asprintf,-u,__wrap_media_lib_asprintf,--wrap=snprintf,-u,__wrap_snprintf")
# endif()

# The standalone protocol component owns both headers and the prebuilt library.
# ADF exposes a different esp_rtc_config_t layout when its copy is enabled.
if(CONFIG_MEDIA_PROTOCOL_LIB_ENABLE)
    message(FATAL_ERROR "Disable CONFIG_MEDIA_PROTOCOL_LIB_ENABLE in menuconfig; this project uses espressif/esp_media_protocols from the component registry.")
endif()

# Advertise FU-A packetization and send-only camera video in SIP SDP.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=media_lib_asprintf,-u,__wrap_media_lib_asprintf,--wrap=snprintf,-u,__wrap_snprintf")
