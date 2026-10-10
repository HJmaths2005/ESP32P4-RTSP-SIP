#include"internship.h"


//should stay in this file 
static const char *TAG = "main";
esp_lcd_panel_handle_t lcd_panel = NULL; 
_lock_t lvgl_api_lock;
lv_display_t* display;
esp_lcd_touch_handle_t touch_handler = NULL;
char ip_address[16] = {0};
char* ip ;
esp_rtc_handle_t rtc_handler = NULL;
i2c_master_bus_handle_t i2c_handle = NULL;
extern volatile uint32_t s_isr_count;
extern uint8_t use_h264;
TaskHandle_t ip_updater_task;
char* path;
//buffer that will be used for RTSP

void print_log(void* args){
    while(1){
    ESP_LOGW(TAG,"isr count is equal to : %d" , s_isr_count);
    vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void ip_check(void* args){
    while(1){
        ui_update_ip_label(); 
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
void app_main(void)
{
    wifi_init();
    path = malloc(sizeof(char) * 100);

    ESP_LOGI(TAG,"wifi intialization and stationary connection success");
    ESP_LOGI(TAG,"our esp ip addres is %s\n" ,ip_address);
    lcd_panel = init_lcd();
    //initializing lcd 
    ESP_LOGI(TAG , "lcd_initialization finished");
    //initializing capacitative touch sensor module 
    i2c_init();
    touch_handler = touch_init();
    ESP_LOGI(TAG, "touch module initialization finished");
    //initializing graphical library 
    graphical_lib_init(lcd_panel,display);
    ESP_LOGI(TAG, "lvgl initialization finished");
    _lock_acquire(&lvgl_api_lock);
    ui_main_page();
    ui_register_wifi_cb(); 
    _lock_release(&lvgl_api_lock);
    ESP_LOGI(TAG, "[ 2 ] Initialize av stream");
    // Initialize the audio ADC device via board manager
    
    
    // ESP_ERROR_CHECK(audio_codec_init(8000));
    
    // esp_log_level_set("CSI", ESP_LOG_DEBUG);
    // esp_log_level_set("cam_ctlr",   ESP_LOG_VERBOSE);
    // esp_log_level_set("CSI",        ESP_LOG_VERBOSE);
    // esp_log_level_set("ISP",        ESP_LOG_VERBOSE);
    // esp_log_level_set("isp_awb",    ESP_LOG_DEBUG);


    xTaskCreate(ip_check, "ip_check,",4096,NULL,1,&ip_updater_task);
    ESP_ERROR_CHECK(camera_init(lcd_panel));   // pass the same panel handle
    ESP_ERROR_CHECK(camera_start());

    // rtc_handler = sip_service_start("udp://103:1@10.163.220.165:5060");
    // esp_rtc_call(rtc_handler, "102@10.163.220.165:5060");
    // sprintf(path, "rtsp://%s:8554/esp32p4cam",ip_address);
    // rtsp_sservice_start(RTSP_SERVER,/*"rtsp://10.156.65.165:8554/esp32p4cam"*/path);
    // audio_dac_test_tone();
}

