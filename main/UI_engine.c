#include "UI_engine.h"
#include "VoIP_manip.h"
#include "app_validation.h"



//voice manipulatin module 
//web RTC
//SIP (voip)

extern lv_display_t* display;
extern lv_indev_t* touch;
extern char* ip;
extern _lock_t lvgl_api_lock; 
extern char path[100];
extern char ip_address[16];
static esp_rtsp_handle_t s_rtsp = NULL;
extern esp_rtc_handle_t rtc_handler;
static void settings_save_cb(lv_event_t *e);
static void voice_register_cb(lv_event_t *e);
static void voice_answer_cb(lv_event_t *e);
static void ui_service_timer_cb(lv_timer_t *timer);
static lv_obj_t *s_main_status;
static lv_obj_t *s_stream_status;
static lv_obj_t *s_sip_user_ta;
static lv_obj_t *s_sip_pass_ta;
static char s_sip_user[32];
static char s_sip_password[64];
static char s_sip_uri[512];
static char s_stream_address[16];
static uint32_t s_page_generation;
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
char sip_server_ip[64];

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

static void ui_place(lv_obj_t *obj, int w, int h, int x, int y)
{
    /* Keyboard defaults to BOTTOM_MID; explicit positions use TOP_LEFT. */
    lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
}

static lv_obj_t *ui_text(lv_obj_t *parent, const char *text, int w, int h, int x, int y, bool heading)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    ui_place(label, w, h, x, y);
    lv_obj_set_style_text_font(label, heading ? &lv_font_montserrat_30 : &lv_font_montserrat_20, 0);
    return label;
}

static void ui_button_style(lv_obj_t *button, bool danger)
{
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(danger ? 0x9F3548 : 0x167D8D), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x3B6075), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(0xF3F7FC), 0);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x255A70), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x273447), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(0x8B9AAE), LV_STATE_DISABLED);
}

static void ui_field_style(lv_obj_t *field)
{
    lv_obj_set_style_radius(field, 14, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x17263B), 0);
    lv_obj_set_style_text_color(field, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(field, &lv_font_montserrat_20, 0);
    lv_obj_set_style_border_color(field, lv_color_hex(0x3B6075), 0);
    lv_obj_set_style_border_color(field, lv_color_hex(0x58D5C9), LV_STATE_FOCUSED);
    lv_obj_set_style_pad_all(field, 16, 0);
}

static lv_obj_t *ui_action(lv_obj_t *scr, const char *text, int w, int x, int y, lv_event_cb_t cb, bool danger)
{
    lv_obj_t *button = lv_button_create(scr);
    ui_place(button, w, 64, x, y);
    ui_button_style(button, danger);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    return button;
}


//a function that takes the active screen and then sets initial values like colors and adds a caption
static lv_obj_t *ui_begin(const char *title, const char *subtitle)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    free_resources();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, UI_SCREEN_BG_COLOR, 0);
    lv_obj_set_style_text_color(scr, UI_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_20, 0);
    ui_text(scr, title, 940, 48, 40, 30, true);
    lv_obj_t *caption = ui_text(scr, subtitle, 1120, 60, 40, 94, false);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xA8B8CC), 0);
    return scr;
}

static lv_obj_t *ui_card(lv_obj_t *scr, const char *symbol, const char *title,
                         const char *description, int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *card = lv_button_create(scr);
    ui_place(card, 580, 210, x, y);
    ui_button_style(card, false);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x17263B), 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x58D5C9), 0);
    lv_obj_set_pos(icon, 0, 0);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(name, 56, 0);
    lv_obj_t *detail = lv_label_create(card);
    lv_label_set_text(detail, description);
    lv_obj_set_width(detail, 520);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xA8B8CC), 0);
    lv_obj_set_pos(detail, 0, 78);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);
    return card;
}

//main page code 
void ui_main_page(void)
{
    lv_obj_t *scr = ui_begin("Camera & calls", "Connect your network, then start a stream or make a call.");
    main_page_ip_address_label = ui_text(scr, "WiFi: disconnected", 1100, 42, 40, 158, false);
    main_page_wifi_button = ui_card(scr, LV_SYMBOL_WIFI, "WiFi", "Connect or change your wireless network.", 40, 224, main_page_wifi_button_clicked_cb);
    main_page_video_button = ui_card(scr, LV_SYMBOL_VIDEO, "Live video", "Start the camera stream and view its playback address.", 660, 224, streaming_clicked_cb);
    main_page_voice_button = ui_card(scr, LV_SYMBOL_CALL, "Voice calls", "Register your account, dial, or answer an incoming call.", 40, 472, voice_call_clicked_cb);
    main_page_filesys_button = ui_card(scr, LV_SYMBOL_SETTINGS, "Settings", "Manage your SIP server and account.", 660, 472, settings_clicked_cb);
    s_main_status = ui_text(scr, "Start with WiFi if this is your first connection.", 1160, 60, 40, 722, false);
}


void ui_wifi_connection_page(void)
{
    lv_obj_t *scr = ui_begin("WiFi connection", "Enter the network name and password. Leave the password empty for an open network.");
    ui_action(scr, "Back", 180, 1060, 30, wifi_back_cb, false);
    ui_text(scr, "Network name", 900, 30, 40, 168, false);
    s_ssid_ta = lv_textarea_create(scr);
    ui_field_style(s_ssid_ta);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_max_length(s_ssid_ta, 32);
    lv_textarea_set_placeholder_text(s_ssid_ta, "SSID");
    ui_place(s_ssid_ta, 1160, 68, 40, 208);
    lv_obj_add_event_cb(s_ssid_ta, ta_event_cb, LV_EVENT_ALL, NULL);
    ui_text(scr, "Password", 900, 30, 40, 294, false);
    s_pass_ta = lv_textarea_create(scr);
    ui_field_style(s_pass_ta);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_max_length(s_pass_ta, 63);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "Network password");
    ui_place(s_pass_ta, 1160, 68, 40, 334);
    lv_obj_add_event_cb(s_pass_ta, ta_event_cb, LV_EVENT_ALL, NULL);
    ui_action(scr, "Connect", 220, 40, 426, connect_clicked_cb, false);
    ui_action(scr, "Disconnect", 220, 288, 426, disconnect_clicked_cb, true);
    s_status_lb = ui_text(scr, "Ready to connect", 650, 64, 552, 426, false);
    char address[16];
    if (wifi_get_ip(address, sizeof(address))) lv_label_set_text_fmt(s_status_lb, "Connected: %s", address);
    s_kb = lv_keyboard_create(scr);
    ui_place(s_kb, 1224, 240, 28, 540);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x17263B), 0);
    lv_obj_set_click_focusable(s_kb, false);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(s_kb, s_ssid_ta);
}


void video_streaming_page(void)
{
    lv_obj_t *scr = ui_begin("Live video", "Keep this page open while watching the camera from another device.");
    s_stream_status = ui_text(scr, "Stream active", 1160, 40, 40, 164, false);
    lv_obj_set_style_text_color(s_stream_status, lv_color_hex(0x58D5C9), 0);
    ui_text(scr, "PLAYBACK ADDRESS", 1160, 36, 40, 224, false);
    ui_text(scr, path, 1160, 100, 40, 282, true);
    ui_text(scr, "Open this address in your RTSP player on the same network.", 1160, 80, 40, 422, false);
    streaming_stop_button = ui_action(scr, "Stop streaming", 300, 40, 610, streaming_stop_button_clicked_cb, true);
    streaming_stop_label = lv_obj_get_child(streaming_stop_button, 0);
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

    
    extern char ip_address[16];       // the real buffer, defined in internship.c

    _lock_acquire(&lvgl_api_lock);
    if (!main_page_ip_address_label || !lv_obj_is_valid(main_page_ip_address_label)) {
        main_page_ip_address_label = NULL;   // self-heal
        _lock_release(&lvgl_api_lock);
        return;
    }
    char address[16];
    if (!wifi_get_ip(address, sizeof(address))) {
        lv_label_set_text(main_page_ip_address_label, "IP: N/A");
    } else {
        lv_label_set_text_fmt(main_page_ip_address_label, "IP: %s", address);
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
    lv_timer_create(ui_service_timer_cb, 250, NULL);
}

void free_resources(void){
    ++s_page_generation;
    s_main_status = NULL;
    s_stream_status = NULL;
    s_sip_user_ta = NULL;
    s_sip_pass_ta = NULL;
    streaming_stop_button = NULL;
    streaming_stop_label = NULL;
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



static lv_obj_t *form_field(lv_obj_t *scr, const char *label,
                            const char *value, size_t max_length, bool secret, int y)
{
    lv_obj_t *caption = lv_label_create(scr);
    lv_label_set_text(caption, label);
    ui_place(caption, 1100, 30, 40, 800 - y);
    lv_obj_t *field = lv_textarea_create(scr);
    ui_field_style(field);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, max_length);
    lv_textarea_set_password_mode(field, secret);
    lv_textarea_set_text(field, value);
    ui_place(field, 1160, 68, 40, 832 - y);
    lv_obj_add_event_cb(field, ta_event_cb, LV_EVENT_ALL, NULL);
    return field;
}

static void form_button(lv_obj_t *scr, const char *text, int x, int y, lv_event_cb_t cb)
{
    ui_action(scr, text, 180, x, 800 - y, cb, cb == voice_hangup_clicked_cb);
}

static void form_keyboard(lv_obj_t *scr)
{
    s_kb = lv_keyboard_create(scr);
    ui_place(s_kb, 1224, 240, 28, 540);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x17263B), 0);
    lv_obj_set_click_focusable(s_kb, false);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
}

void ui_settings_page(void)
{
    lv_obj_t *scr = ui_begin("SIP settings", "Set up your server and account before making a call.");
    form_button(scr, "Back", 1060, 770, wifi_back_cb);
    s_sip_ta = form_field(scr, "SIP server (IPv4 or hostname)", sip_server_ip, 63, false, 650);
    s_sip_user_ta = form_field(scr, "SIP account", s_sip_user, 31, false, 550);
    s_sip_pass_ta = form_field(scr, "SIP password", s_sip_password, 63, true, 450);
    form_button(scr, "Save", 40, 330, settings_save_cb);
    s_sip_status = lv_label_create(scr);
    lv_label_set_text(s_sip_status, "Port 5060 / UDP. Settings last until restart.");
    ui_place(s_sip_status, 900, 64, 280, 470);
    form_keyboard(scr);
    lv_keyboard_set_textarea(s_kb, s_sip_ta);
}

void ui_voice_call_page(void)
{
    lv_obj_t *scr = ui_begin("Voice calls", "Register your account, then dial an extension or answer a call.");
    form_button(scr, "Back", 1060, 770, wifi_back_cb);
    s_voice_ta = form_field(scr, "Extension", s_dialed_number, 31, false, 600);
    lv_textarea_set_accepted_chars(s_voice_ta, "0123456789*#+");
    form_button(scr, "Register", 40, 410, voice_register_cb);
    form_button(scr, "Call", 260, 410, voice_dial_clicked_cb);
    form_button(scr, "Answer", 480, 410, voice_answer_cb);
    form_button(scr, "Hang up", 700, 410, voice_hangup_clicked_cb);
    s_voice_status = lv_label_create(scr);
    lv_label_set_text(s_voice_status, "Configure account in settings, then register.");
    ui_place(s_voice_status, 1160, 64, 40, 470);
    form_keyboard(scr);
    lv_keyboard_set_textarea(s_kb, s_voice_ta);
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
    esp_err_t ret = wifi_connect(ssid, pass);
    if (ret != ESP_OK && s_status_lb) {
        lv_label_set_text(s_status_lb, ret == ESP_ERR_INVALID_ARG
            ? "SSID: 1-32 bytes. Password: 8-63 bytes or 64 hex digits."
            : "Connection request failed; try again.");
    }
}


void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    /* Pointer taps must open the keyboard even without an input focus group,
     * and after Done when the textarea is already focused. */
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED && code != LV_EVENT_FOCUSED) return;
    if (!s_kb) return;
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
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
    if (wifi_disconnect() != ESP_OK && s_status_lb) lv_label_set_text(s_status_lb, "Disconnect request failed.");
}

void streaming_clicked_cb(lv_event_t *e){

    char address[16];
    if (!wifi_get_ip(address, sizeof(address))) {
        if (s_main_status) lv_label_set_text(s_main_status, "Connect WiFi before streaming.");
        ESP_LOGW(TAG, "no IP yet, not starting RTSP");
        return;
    }
    if (s_rtsp) {
        ESP_LOGW(TAG, "RTSP already running, ignoring press");
        return;
    }
    snprintf(path, sizeof(path), "rtsp://%s:8554/esp32p4cam", address);
    ESP_LOGI(TAG, "starting RTSP at %s", path);
    s_rtsp = rtsp_service_start(RTSP_SERVER, path);
    if (!s_rtsp) {
        ESP_LOGE(TAG, "RTSP start failed");
        if (s_main_status) lv_label_set_text(s_main_status, "Stream could not start; try again.");
        return;
    }
    memcpy(s_stream_address, address, sizeof(address));
    video_streaming_page();

}

void streaming_stop_button_clicked_cb(lv_event_t *e){
    if (s_rtsp) {
        if (rtsp_service_stop(s_rtsp) != ESP_OK) {
            if (streaming_stop_label) lv_label_set_text(streaming_stop_label, "stop failed; retry");
            return;
        }
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
    if (!s_sip_ta || !s_sip_user_ta || !s_sip_pass_ta) return;
    const char *host = lv_textarea_get_text(s_sip_ta);
    const char *user = lv_textarea_get_text(s_sip_user_ta);
    const char *password = lv_textarea_get_text(s_sip_pass_ta);
    if (!app_sip_host_valid(host) || !user[0]) {
        lv_label_set_text(s_sip_status, "Enter a valid server and account.");
        return;
    }
    if (rtc_handler && sip_service_in_call()) {
        lv_label_set_text(s_sip_status, "Hang up before changing the account.");
        return;
    }
    if (rtc_handler) {
        if (sip_service_stop(rtc_handler) != ESP_OK) {
            lv_label_set_text(s_sip_status, "Account could not stop; try again.");
            return;
        }
        rtc_handler = NULL;
    }
    snprintf(sip_server_ip, sizeof(sip_server_ip), "%s", host);
    snprintf(s_sip_user, sizeof(s_sip_user), "%s", user);
    snprintf(s_sip_password, sizeof(s_sip_password), "%s", password);
    lv_label_set_text(s_sip_status, "Saved until restart. Register on the voice page.");
}

void voice_call_clicked_cb(lv_event_t *e) { 
    ui_voice_call_page(); 
    // rtc_handler = sip_service_start("udp://103:1@172.23.138.165:5060");
    // esp_rtc_call(rtc_handler,"102@172.23.138.165:5060");
    // test_udp();
}

static void voice_register_cb(lv_event_t *e)
{
    esp_log_level_set("SIP", ESP_LOG_VERBOSE);
esp_log_level_set("TRANS_UDP", ESP_LOG_VERBOSE);
esp_log_level_set("ESP_RTC_CORE", ESP_LOG_VERBOSE);
esp_log_level_set("ESP_RTC", ESP_LOG_VERBOSE);
esp_log_level_set("MEDIA_LIB", ESP_LOG_VERBOSE);


    if (!s_voice_status) return;
    if (!wifi_is_connected()) { lv_label_set_text(s_voice_status, "Connect WiFi first."); return; }
    if (!g_codec_dev) { lv_label_set_text(s_voice_status, "Audio is unavailable."); return; }
    if (!app_sip_host_valid(sip_server_ip) || !s_sip_user[0]) {
        lv_label_set_text(s_voice_status, "Configure your SIP account in settings."); return;
    }
    if (rtc_handler) {
        if (!sip_service_registered() && sip_service_last_event() == ESP_RTC_EVENT_ERROR) {
            if (sip_service_stop(rtc_handler) != ESP_OK) {
                ESP_LOGE(TAG,"not being registered");
                lv_label_set_text(s_voice_status, "Registration could not reset; try again.");
                return;
            }
            rtc_handler = NULL;
        }
    }
    if (rtc_handler) {
        lv_label_set_text(s_voice_status, sip_service_registered() ? "Registered." : "Registration is in progress.");
        return;
    }
    char user[sizeof(s_sip_user) * 3], password[sizeof(s_sip_password) * 3];
    if (!app_uri_escape(s_sip_user, user, sizeof(user)) ||
        !app_uri_escape(s_sip_password, password, sizeof(password))) {
        lv_label_set_text(s_voice_status, "Account is too long."); return;
    }
    snprintf(s_sip_uri, sizeof(s_sip_uri), "udp://%s:%s@%s:5060", user, password, sip_server_ip);
    // ESP_LOGE(TAG,"the uri for registering is %s" , s_sip_uri);
    rtc_handler = sip_service_start(s_sip_uri);
    lv_label_set_text(s_voice_status, rtc_handler ? "Registering..." : "Registration could not start.");
}

void voice_dial_clicked_cb(lv_event_t *e)
{
    if (!s_voice_status || !s_voice_ta) return;
    if (!wifi_is_connected() || !rtc_handler || !sip_service_registered()) {
        lv_label_set_text(s_voice_status, "Connect WiFi and register first."); return;
    }
    if (sip_service_in_call()) { lv_label_set_text(s_voice_status, "A call is already active."); return; }
    const char *number = lv_textarea_get_text(s_voice_ta);
    if (!number[0]) { lv_label_set_text(s_voice_status, "Enter an extension."); return; }
    snprintf(s_dialed_number, sizeof(s_dialed_number), "%s", number);
    char remote[128];
    snprintf(remote, sizeof(remote), "%s@%s:5060", s_dialed_number, sip_server_ip);
    int ret = esp_rtc_call(rtc_handler, remote);
    lv_label_set_text(s_voice_status, ret == ESP_OK ? "Calling..." : "Call could not start.");
}

static void voice_answer_cb(lv_event_t *e)
{
    if (!rtc_handler || !s_voice_status) return;
    if (!sip_service_incoming()) { lv_label_set_text(s_voice_status, "No incoming call."); return; }
    int ret = esp_rtc_answer(rtc_handler);
    lv_label_set_text(s_voice_status, ret == ESP_OK ? "Answering..." : "Could not answer.");
}

void voice_hangup_clicked_cb(lv_event_t *e)
{
    if (!rtc_handler || !s_voice_status) return;
    int ret = esp_rtc_bye(rtc_handler);
    lv_label_set_text(s_voice_status, ret == ESP_OK ? "Hanging up..." : "No call to hang up.");
}

static void ui_service_timer_cb(lv_timer_t *timer)
{
    char address[16];
    bool connected = wifi_get_ip(address, sizeof(address));
    if (main_page_ip_address_label) {
        char network_text[96];
        snprintf(network_text, sizeof(network_text), connected ? "WiFi connected  |  IP: %s" : "WiFi disconnected  |  Open WiFi to connect", address);
        if (strcmp(lv_label_get_text(main_page_ip_address_label), network_text)) {
            lv_label_set_text(main_page_ip_address_label, network_text);
            lv_obj_set_style_text_color(main_page_ip_address_label, lv_color_hex(connected ? 0x58D5C9 : 0xF1BF76), 0);
        }
    }
    if (main_page_video_button) {
        if (connected) lv_obj_remove_state(main_page_video_button, LV_STATE_DISABLED);
        else lv_obj_add_state(main_page_video_button, LV_STATE_DISABLED);
    }

    if (s_rtsp && (!connected || strcmp(address, s_stream_address))) {
        if (rtsp_service_stop(s_rtsp) == ESP_OK) {
            s_rtsp = NULL;
            if (streaming_stop_label) lv_label_set_text(streaming_stop_label, "Back to home");
            if (s_stream_status) lv_label_set_text(s_stream_status, "Stream stopped: WiFi changed. Reconnect and start again.");
        }
    }
    if (rtc_handler && !connected) {
        if (sip_service_stop(rtc_handler) == ESP_OK) rtc_handler = NULL;
        if (s_voice_status) lv_label_set_text(s_voice_status, "WiFi disconnected. Reconnect and register.");
        return;
    }
    static esp_rtc_event_t previous = ESP_RTC_EVENT_NULL;
    static uint32_t previous_generation;
    esp_rtc_event_t event = sip_service_last_event();
    if (s_main_status && rtc_handler && sip_service_incoming()) lv_label_set_text(s_main_status, "Incoming call. Open voice call to answer.");
    if (!s_voice_status || !rtc_handler || (event == previous && previous_generation == s_page_generation)) return;
    previous = event;
    previous_generation = s_page_generation;
    switch (event) {
        case ESP_RTC_EVENT_REGISTERED: lv_label_set_text(s_voice_status, "Registered. Ready to call."); break;
        case ESP_RTC_EVENT_UNREGISTERED: lv_label_set_text(s_voice_status, "Registering..."); break;
        case ESP_RTC_EVENT_CALLING: lv_label_set_text(s_voice_status, "Calling..."); break;
        case ESP_RTC_EVENT_INCOMING: lv_label_set_text(s_voice_status, "Incoming call. Answer or hang up."); break;
        case ESP_RTC_EVENT_CALL_ANSWERED:
        case ESP_RTC_EVENT_AUDIO_SESSION_BEGIN: lv_label_set_text(s_voice_status, "Call connected."); break;
        case ESP_RTC_EVENT_HANGUP:
        case ESP_RTC_EVENT_AUDIO_SESSION_END: lv_label_set_text(s_voice_status, "Call ended."); break;
        case ESP_RTC_EVENT_ERROR: lv_label_set_text(s_voice_status, "SIP error. Check account and server."); break;
        default: break;
    }
}
