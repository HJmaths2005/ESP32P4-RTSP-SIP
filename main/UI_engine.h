#pragma once

#include "lvgl.h"
#include "common.h"
#include "WIFI_manip.h"
#include "sys/lock.h"
#include "camera_manip.h"



#define UI_BUTTON_BOTTOM_BG_COLOR lv_color_hex(0x1A2037)
#define UI_SCREEN_BG_COLOR lv_color_hex(0x0B1120)
#define UI_BUTTON_BORDER_COLOR lv_color_hex(0x3A4DFF)
#define UI_TEXT_COLOR lv_color_hex(0xF3F7FC)
//0x164678
//0x0A2037 this is some unique ones the upper border fades
// #define UI_BUTTON_TOP_BG_COLOR lv_color_hex(0x011178)
#define UI_BUTTON_TOP_BG_COLOR lv_color_hex(0x000000)


void ui_settings_page(void);
void settings_clicked_cb(lv_event_t *e);

void ui_voice_call_page(void);
void ui_video_call_page(void);
void video_call_clicked_cb(lv_event_t *e);
void voice_call_clicked_cb(lv_event_t *e);   /* from main page, opens the dial page */
void voice_dial_clicked_cb(lv_event_t *e);   /* from dial page, places the call    */
void voice_hangup_clicked_cb(lv_event_t *e); /* from in-call page                  */



void ui_main_page(void);
void show_red_page(void);
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
    int32_t text_size);



void new_info_label(lv_obj_t **screen,lv_obj_t **label,int32_t size_x, 
    int32_t size_y ,int32_t pos_x , int32_t pos_y ,
    char* text, int32_t font);

void place_landscape(lv_obj_t *obj,
                     int32_t lx, int32_t ly,
                     int32_t lw, int32_t lh);
void ui_update_ip_label(void);  


void main_page_button_clicked_cb(lv_event_t *e);
void dummy_page_with_exit_button(void);
void dummy_exit_button_cb(lv_event_t *e);
void ui_voice_clicked_cb(lv_event_t *e);
void main_page_wifi_button_clicked_cb(lv_event_t *e);
void wifi_back_cb(lv_event_t * e);

void connect_clicked_cb(lv_event_t *e);
void ta_event_cb(lv_event_t *e);
void kb_event_cb(lv_event_t *e);
void disconnect_clicked_cb(lv_event_t *e);



void wifi_state_cb(wifi_state_t st, const char *ip);
void ui_register_wifi_cb(void);
void free_resources(void);
void incoming_call_page(void);
void decline_call_back(lv_event_t* e);
void answer_call_back(lv_event_t* e);
void incall_page(void);