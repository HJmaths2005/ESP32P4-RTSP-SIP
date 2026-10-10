#include "UI_engine.h"



//voice manipulatin module 
//web RTC
//SIP (voip)

extern lv_display_t* display;
extern lv_indev_t* touch;
extern char* ip;
extern _lock_t lvgl_api_lock; 
extern char* path;
extern char ip_address[16];
static esp_rtsp_handle_t s_rtsp = NULL;
// extern esp_rtc_handle_t rtc_handler;   /* lives in internship.c */
/////////////////////////////////////////////////////////////
//global lvgl object variables///////////////////////////////
/////////////////////////////////////////////////////////////


//main page variables/////////////////////////////////////////
lv_obj_t* main_page_wifi_button= NULL;
lv_obj_t* main_page_wifi_label = NULL;
lv_obj_t* main_page_voice_button= NULL;
lv_obj_t* main_page_voice_label = NULL;
lv_obj_t* main_page_video_button= NULL;
lv_obj_t* main_page_video_label = NULL;
lv_obj_t* main_page_filesys_button= NULL;
lv_obj_t* main_page_filesys_label = NULL;
lv_obj_t* main_page_about_button= NULL;
lv_obj_t* main_page_about_label = NULL;
lv_obj_t* main_page_ip_address_label = NULL;
////////////////////////////////////////////////////

////////////////////////////////////////////////////
//streaming page variables
lv_obj_t* streaming_stop_button = NULL;
lv_obj_t* streaming_stop_label = NULL;
//////////////////////////////////////////


///////////////////////////////////////////
//settings page variables//////////////////
///////////////////////////////////////////
char sip_server_ip[64] = "10.163.220.165";

static lv_obj_t *s_sip_ta     = NULL;
static lv_obj_t *s_sip_status = NULL;


/////////////////////////////////////////////////////
/////////////////voip parameters/////////////////////
/////////////////////////////////////////////////////
/* Voice dial page */
static lv_obj_t *s_voice_ta     = NULL;
static lv_obj_t *s_voice_status = NULL;

/* In-call page */
static lv_obj_t *s_call_info    = NULL;
static lv_obj_t *s_hangup_btn   = NULL;
static lv_obj_t *s_hangup_lbl   = NULL;

/* Last dialed number, kept so the in-call page can display it */
static char s_dialed_number[32] = {0};




///////////dummy page variables//////////////////////
lv_obj_t* dummy_page_back_button = NULL;
lv_obj_t* dummy_page_back_button_label = NULL;
/////////////////////////////////////////////////////


static lv_obj_t *s_ssid_ta   = NULL;
static lv_obj_t *s_pass_ta   = NULL;
static lv_obj_t *s_kb        = NULL;
static lv_obj_t *s_status_lb = NULL;
const char* TAG = "UI_engine";

//////////////////////////////////////////////////////////////////////
///////////////////////////page lvgl codes////////////////////////////
//////////////////////////////////////////////////////////////////////

void ui_main_page(void)
{
    lv_obj_t * screen = lv_screen_active();
    lv_obj_clean(screen);
    free_resources();
    lv_obj_set_style_bg_color(screen, UI_SCREEN_BG_COLOR, 0);
    lv_obj_set_style_text_color(screen, UI_TEXT_COLOR, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);




    //ip text label 
    new_info_label(&screen, &main_page_ip_address_label,30,20,10,790,"N/A",14);

    //main page buttons 
    new_button_obj("wifi",
    &screen,
    250,
    250,
    340,
    670,
    &main_page_wifi_button,
    UI_BUTTON_TOP_BG_COLOR,
    UI_BUTTON_BOTTOM_BG_COLOR,
    UI_BUTTON_BORDER_COLOR,
    &main_page_wifi_label,
    70,
    200,
    18);
    
    new_button_obj("video streaming",
    &screen,
    250,
    250,
    690,
    670,
    &main_page_video_button,
    UI_BUTTON_TOP_BG_COLOR,
    UI_BUTTON_BOTTOM_BG_COLOR,
    UI_BUTTON_BORDER_COLOR,
    &main_page_video_label,
    40,
    200,
    18);
    
    new_button_obj("voice call",
    &screen, 
    250,
    250,
    340,
    300,
    &main_page_voice_button,
    UI_BUTTON_TOP_BG_COLOR,
    UI_BUTTON_BOTTOM_BG_COLOR,
    UI_BUTTON_BORDER_COLOR,
    &main_page_voice_label,
    70,
    200,
    18);
    
    new_button_obj("settings ",
    &screen,
    250,
    250,
    690,
    300,
    &main_page_filesys_button,
    UI_BUTTON_TOP_BG_COLOR,
    UI_BUTTON_BOTTOM_BG_COLOR,
    UI_BUTTON_BORDER_COLOR,
    &main_page_filesys_label,
    70,
    200,
    18);


    lv_obj_add_event_cb(main_page_wifi_button, main_page_wifi_button_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(main_page_video_button, streaming_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(main_page_filesys_button, settings_clicked_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(main_page_voice_button, voice_call_clicked_cb, LV_EVENT_CLICKED, NULL);
    
}

void ui_wifi_connection_page(void){
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    free_resources();
    lv_obj_set_style_bg_color(scr, UI_SCREEN_BG_COLOR, 0);
    lv_obj_set_style_text_color(scr, UI_TEXT_COLOR, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    
    
    s_ssid_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_placeholder_text(s_ssid_ta, "SSID");
    place_landscape(s_ssid_ta,400,50,440,600);
    lv_obj_add_event_cb(s_ssid_ta, ta_event_cb, LV_EVENT_ALL, NULL);
    
    s_pass_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "password");
    place_landscape(s_pass_ta, 400, 50, 440, 540);
    lv_obj_add_event_cb(s_pass_ta, ta_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb,"connect");
    lv_obj_center(lb);
    place_landscape(btn, 100, 50, 440, 480);
    lv_obj_add_event_cb(btn, connect_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back = lv_button_create(scr);
    // lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "back");
    lv_obj_center(bl);
    place_landscape(back, 100, 50, 10, 780);
    lv_obj_add_event_cb(back, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    
    s_status_lb = lv_label_create(scr);
    lv_label_set_text(s_status_lb, "enter credentials");
    place_landscape(s_status_lb, 150, 80, 440, 380);
    
    s_kb = lv_keyboard_create(scr);
    

    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);


    lv_obj_t *disconneting_button = lv_button_create(scr);
    lv_obj_t *disconneting_label = lv_label_create(disconneting_button);
    lv_label_set_text(disconneting_label, "disconnect");
    lv_obj_center(disconneting_label);
    place_landscape(disconneting_button, 100, 50, 560, 480);
    lv_obj_add_event_cb(disconneting_button, disconnect_clicked_cb, LV_EVENT_CLICKED, NULL);
    
}

void video_streaming_page(void){
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    free_resources();
    lv_obj_set_style_bg_color(screen, UI_SCREEN_BG_COLOR, 0);
    lv_obj_set_style_text_color(screen, UI_TEXT_COLOR, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    
    new_button_obj("stop streaming",
    &screen,155,50,10,790,&streaming_stop_button,lv_color_hex(0xFF0000),
    lv_color_hex(0xFF0000),lv_color_hex(0xFF0000)
    ,&streaming_stop_label,10,10,16);

    lv_obj_add_event_cb(streaming_stop_button, streaming_stop_button_clicked_cb, LV_EVENT_CLICKED, NULL);
}

//////////////////////////////////////////////////////////////////////
//////////////////////////helper functions////////////////////////////
//////////////////////////////////////////////////////////////////////
void new_button_obj(char * button_text, 
    lv_obj_t **screen,
    int32_t button_x, 
    int32_t button_y,
    int32_t button_pos_x,
    int32_t button_pos_y,
    lv_obj_t **button,
    lv_color_t button_top_color,
    lv_color_t button_bottom_color,
    lv_color_t button_border_color,
    lv_obj_t **label,
    int32_t label_x,
    int32_t label_y, 
    int32_t text_size)

{
    //added for landscape orientation
    int real_button_pos_x = button_pos_y;
    int real_button_pos_y = button_pos_x;
    /////////////////////////////////////////////////////////
    
    *button = lv_button_create(*screen);
    *label = lv_label_create(*button);
    lv_obj_set_style_bg_color(*button, button_top_color,0);
    lv_obj_set_style_bg_grad_color(*button , button_bottom_color,0);
    lv_obj_set_style_bg_grad_dir(*button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(*button, button_border_color,0);
    ///////////////changed for landscape orientation//////////////
    lv_obj_set_pos(*button , real_button_pos_x, real_button_pos_y);
    lv_obj_set_style_transform_angle(*button, 900, 0);
    //////////////////////////////////////////////////////////////
    lv_obj_set_size(*button,button_x,button_y);
    
    lv_obj_set_x(*label , label_x);
    lv_obj_set_y(*label, label_y);
    switch (text_size){
        case 10: lv_obj_set_style_text_font(*label, &lv_font_montserrat_10, 0); break;
        case 12: lv_obj_set_style_text_font(*label, &lv_font_montserrat_12, 0); break;
        case 14: lv_obj_set_style_text_font(*label, &lv_font_montserrat_14, 0); break;
        case 16: lv_obj_set_style_text_font(*label, &lv_font_montserrat_16, 0); break;
        case 18: lv_obj_set_style_text_font(*label, &lv_font_montserrat_18, 0); break;
        case 20: lv_obj_set_style_text_font(*label, &lv_font_montserrat_20, 0); break;
        case 22: lv_obj_set_style_text_font(*label, &lv_font_montserrat_22, 0); break;
        case 24: lv_obj_set_style_text_font(*label, &lv_font_montserrat_24, 0); break;
        case 26: lv_obj_set_style_text_font(*label, &lv_font_montserrat_26, 0); break;
        case 30: lv_obj_set_style_text_font(*label, &lv_font_montserrat_30, 0); break;
        default : ESP_LOGI(TAG,"incorrect font size number for button text size");
    }
    
    lv_label_set_text(*label, button_text);
}


void new_info_label(lv_obj_t **screen, lv_obj_t **label, int32_t size_x, 
    int32_t size_y, int32_t pos_x, int32_t pos_y, 
    char* text, int32_t font)
    
    {
    int real_label_pos_x = pos_y;
    int real_label_pos_y = pos_x;
    *label = lv_label_create(*screen);
    lv_obj_set_style_transform_angle(*label, 900, 0);
    lv_obj_set_x(*label , size_x);
    lv_obj_set_y(*label, size_y);
    lv_obj_set_pos(*label , real_label_pos_x, real_label_pos_y);
    switch (font){
        case 10: lv_obj_set_style_text_font(*label, &lv_font_montserrat_10, 0); break;
        case 12: lv_obj_set_style_text_font(*label, &lv_font_montserrat_12, 0); break;
        case 14: lv_obj_set_style_text_font(*label, &lv_font_montserrat_14, 0); break;
        case 16: lv_obj_set_style_text_font(*label, &lv_font_montserrat_16, 0); break;
        case 18: lv_obj_set_style_text_font(*label, &lv_font_montserrat_18, 0); break;
        case 20: lv_obj_set_style_text_font(*label, &lv_font_montserrat_20, 0); break;
        case 22: lv_obj_set_style_text_font(*label, &lv_font_montserrat_22, 0); break;
        case 24: lv_obj_set_style_text_font(*label, &lv_font_montserrat_24, 0); break;
        case 26: lv_obj_set_style_text_font(*label, &lv_font_montserrat_26, 0); break;
        case 30: lv_obj_set_style_text_font(*label, &lv_font_montserrat_30, 0); break;
        default : ESP_LOGI(TAG,"incorrect font size number for button text size");
    }
    lv_label_set_text(*label, text);

}

void ui_update_ip_label(void)
{
    if (!main_page_ip_address_label) return;
    
    extern char ip_address[16];       // the real buffer, defined in internship.c

    _lock_acquire(&lvgl_api_lock);
    if (!lv_obj_is_valid(main_page_ip_address_label)) {
        main_page_ip_address_label = NULL;   // self-heal
        _lock_release(&lvgl_api_lock);
        return;
    }
    if (ip_address[0] == '\0') {
        lv_label_set_text(main_page_ip_address_label, "IP: N/A");
    } else {
        lv_label_set_text_fmt(main_page_ip_address_label, "IP: %s", ip_address);
        // ip = ip_address;
    }
    _lock_release(&lvgl_api_lock);
}

void place_landscape(lv_obj_t *obj,
                     int32_t size_x, int32_t size_y,
                     int32_t pos_x, int32_t pos_y)
{
    int real_pos_x = pos_y;
    int real_pos_y = pos_x;

    lv_obj_set_size(obj, size_x, size_y);              /* NO swap */
    lv_obj_set_pos (obj, real_pos_x , real_pos_y);  /* compensate for rotation */
    lv_obj_set_style_transform_angle(obj, 900, 0);
}

void ui_register_wifi_cb(void)
{
    wifi_set_state_cb(wifi_state_cb);
}

void free_resources(void){
    main_page_ip_address_label = NULL;
    main_page_wifi_button      = NULL;
    main_page_wifi_label       = NULL;
    main_page_voice_button     = NULL;
    main_page_voice_label      = NULL;
    main_page_video_button     = NULL;
    main_page_video_label      = NULL;
    main_page_filesys_button   = NULL;
    main_page_filesys_label    = NULL;
    main_page_about_button     = NULL;
    main_page_about_label      = NULL;
    s_ssid_ta   = NULL;
    s_pass_ta   = NULL;
    s_kb        = NULL;
    s_status_lb = NULL;
    s_sip_ta     = NULL;
    s_sip_status = NULL;
    s_voice_ta     = NULL;
    s_voice_status = NULL;
    s_call_info    = NULL;
    s_hangup_btn   = NULL;
    s_hangup_lbl   = NULL;
}


void wifi_state_cb(wifi_state_t st, const char *ip)
{
    _lock_acquire(&lvgl_api_lock);

    if (s_status_lb && lv_obj_is_valid(s_status_lb)) {
        switch (st) {
            case WIFI_STATE_IDLE:
                lv_label_set_text(s_status_lb, "idle");
                break;
            case WIFI_STATE_CONNECTING:
                lv_label_set_text(s_status_lb, "connecting...");
                break;
            case WIFI_STATE_CONNECTED:
                lv_label_set_text_fmt(s_status_lb, "connected: %s",
                                      ip ? ip : "?");

                                      break;
            case WIFI_STATE_FAILED:
                lv_label_set_text(s_status_lb, "connection failed");
                break;
        }
    }

    _lock_release(&lvgl_api_lock);
}



void ui_settings_page(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    free_resources();
    lv_obj_set_style_bg_color(scr, UI_SCREEN_BG_COLOR, 0);
    lv_obj_set_style_text_color(scr, UI_TEXT_COLOR, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Back button ---- */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_t *bl   = lv_label_create(back);
    lv_label_set_text(bl, "back");
    lv_obj_center(bl);
    place_landscape(back, 100, 50, 10, 780);
    lv_obj_add_event_cb(back, wifi_back_cb, LV_EVENT_CLICKED, NULL);

    /* ---- SIP server IP field, preloaded with current value ---- */
    s_sip_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_sip_ta, true);
    lv_textarea_set_placeholder_text(s_sip_ta, "SIP server IP");
    lv_textarea_set_max_length(s_sip_ta, sizeof(sip_server_ip) - 1);
    lv_textarea_set_text(s_sip_ta, sip_server_ip);
    place_landscape(s_sip_ta, 400, 50, 440, 600);
    lv_obj_add_event_cb(s_sip_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    /* ---- Save button ---- */
    lv_obj_t *save = lv_button_create(scr);
    lv_obj_t *sl   = lv_label_create(save);
    lv_label_set_text(sl, "save");
    lv_obj_center(sl);
    place_landscape(save, 100, 50, 440, 480);
    lv_obj_add_event_cb(save, settings_save_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Status label ---- */
    s_sip_status = lv_label_create(scr);
    lv_label_set_text(s_sip_status, "enter SIP server IP");
    place_landscape(s_sip_status, 150, 80, 440, 380);

    /* ---- Keyboard (hidden until a textarea is focused) ---- */
    s_kb = lv_keyboard_create(scr);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
}

//////////////////////////////////////////////////////////////////////////
////////////////////////////event callbacks///////////////////////////////
//////////////////////////////////////////////////////////////////////////

void main_page_wifi_button_clicked_cb(lv_event_t *e){
    ui_wifi_connection_page();
}

void wifi_back_cb(lv_event_t * e){
    ui_main_page();
}

void connect_clicked_cb(lv_event_t *e)
{
    if (!s_ssid_ta || !s_pass_ta) return;

    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);

    if (!ssid || ssid[0] == '\0') {
        if (s_status_lb) lv_label_set_text(s_status_lb, "enter an SSID first");
        return;
    }

    if (s_status_lb) lv_label_set_text(s_status_lb, "connecting...");
    wifi_connect(ssid, pass);
}


void ta_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
    if (!s_kb) return;
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}


void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if ((code == LV_EVENT_READY || code == LV_EVENT_CANCEL) && s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

void disconnect_clicked_cb(lv_event_t *e)
{
    if (s_status_lb) lv_label_set_text(s_status_lb, "disconnecting...");
    wifi_disconnect();
}

void streaming_clicked_cb(lv_event_t *e){

    if (ip_address[0] == '\0') {
        ESP_LOGW(TAG, "no IP yet, not starting RTSP");
        return;
    }
    if (s_rtsp) {
        ESP_LOGW(TAG, "RTSP already running, ignoring press");
        return;
    }
    snprintf(path, 100, "rtsp://%s:8554/esp32p4cam", ip_address);
    ESP_LOGI(TAG, "starting RTSP at %s", path);
    s_rtsp = rtsp_service_start(RTSP_SERVER, path);
    if (!s_rtsp) {
        ESP_LOGE(TAG, "RTSP start failed");
    }
    video_streaming_page();

}

void streaming_stop_button_clicked_cb(lv_event_t *e){
    if (s_rtsp) {
        rtsp_service_stop(s_rtsp);
        s_rtsp = NULL;
    }
    ui_main_page();
}

void settings_clicked_cb(lv_event_t *e)
{
    ui_settings_page();
}

static void settings_save_cb(lv_event_t *e)
{
    if (!s_sip_ta) return;

    const char *v = lv_textarea_get_text(s_sip_ta);
    if (!v || v[0] == '\0') {
        if (s_sip_status) lv_label_set_text(s_sip_status, "empty, not saved");
        return;
    }

    strncpy(sip_server_ip, v, sizeof(sip_server_ip) - 1);
    sip_server_ip[sizeof(sip_server_ip) - 1] = '\0';

    if (s_sip_status) lv_label_set_text(s_sip_status, "saved");
    ESP_LOGI(TAG, "SIP server set to '%s'", sip_server_ip);
}
