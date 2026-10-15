#pragma once
#include <stdbool.h>
#include "string.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "media_lib_adapter.h"
#include "media_lib_netif.h"
// #include "audio_tone_uri.h"
// #include "audio_player_int_tone.h"
// #include "esp_peripherals.h"
#include "esp_rtc.h"
#include "audio_manip.h"
#include "esp_g711.h"
#include "esp_timer.h"

// #include "esp_opus.h"



#define PCM_FRAME_SAMPLES   160   /* 20 ms @ 8 kHz */
#define PCM_FRAME_BYTES     (PCM_FRAME_SAMPLES * sizeof(int16_t))

// extern av_stream_handle_t av_handler;

char *_get_network_ip(void);
int _esp_sip_event_handler(esp_rtc_event_t event, void *ctx);
int _send_audio(unsigned char *data, int len, void *ctx);
int _receive_audio(unsigned char *data, int len, void *ctx);
esp_rtc_handle_t sip_service_start(const char *uri);
/* Stop the existing handle before changing mode. Voice mode offers no video. */
esp_rtc_handle_t sip_service_start_mode(const char *uri, bool video);
bool sip_service_has_video(void);
bool sip_service_video_active(void);
int sip_service_stop(esp_rtc_handle_t handle);
bool sip_service_registered(void);
bool sip_service_in_call(void);
bool sip_service_incoming(void);
esp_rtc_event_t sip_service_last_event(void);
