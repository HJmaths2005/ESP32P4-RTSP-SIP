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

/* Scratch buffers (mono PCM) */
static int16_t pcm_mono[PCM_FRAME_SAMPLES];
static int16_t pcm_stereo[PCM_FRAME_SAMPLES * 2];


// extern av_stream_handle_t av_handler;

char *_get_network_ip(void);
int _esp_sip_event_handler(esp_rtc_event_t event, void *ctx);
int _send_audio(unsigned char *data, int len, void *ctx);
int _receive_audio(unsigned char *data, int len, void *ctx);
esp_rtc_handle_t sip_service_start(const char *uri);