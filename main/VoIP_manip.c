/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2023 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "VoIP_manip.h"
#include "WIFI_manip.h"
#include <stdatomic.h>
#include <strings.h>

static const char *TAG = "SIP_SERVICE";
static atomic_bool s_registered;
static atomic_bool s_in_call;
static atomic_bool s_incoming;
static atomic_int s_last_event = ESP_RTC_EVENT_UNREGISTERED;

bool sip_service_registered(void) { return atomic_load(&s_registered); }
bool sip_service_in_call(void) { return atomic_load(&s_in_call); }
bool sip_service_incoming(void) { return atomic_load(&s_incoming); }
esp_rtc_event_t sip_service_last_event(void) { return atomic_load(&s_last_event); }

char *_get_network_ip(void)
{
    static char address[16];
    return wifi_get_ip(address, sizeof(address)) ? address : NULL;
}

int _esp_sip_event_handler(esp_rtc_event_t event, void *ctx)
{
    atomic_store(&s_last_event, event);
    switch (event) {
        case ESP_RTC_EVENT_REGISTERED: atomic_store(&s_registered, true); 
        // ESP_LOGE(TAG,"user has been registered");break;
        case ESP_RTC_EVENT_UNREGISTERED: atomic_store(&s_registered, false); break;
        case ESP_RTC_EVENT_CALLING: atomic_store(&s_in_call, true); break;
        case ESP_RTC_EVENT_INCOMING:
            atomic_store(&s_incoming, true);
            break;
        case ESP_RTC_EVENT_CALL_ANSWERED:atomic_store(&s_in_call, true);break;
        case ESP_RTC_EVENT_AUDIO_SESSION_BEGIN:
            atomic_store(&s_incoming, false);
            atomic_store(&s_in_call, true);
            break;
        case ESP_RTC_EVENT_HANGUP:
        case ESP_RTC_EVENT_AUDIO_SESSION_END:
            atomic_store(&s_incoming, false);
            atomic_store(&s_in_call, false);
            break;
        case ESP_RTC_EVENT_ERROR:
            atomic_store(&s_registered, false);
            atomic_store(&s_incoming, false);
            atomic_store(&s_in_call, false);
            break;
        default: break;
    }
    ESP_LOGI(TAG, "SIP event %d", event);
    return ESP_OK;
}

int _send_audio(unsigned char *data, int len, void *ctx)
{
    if (!data || len <= 0) return 0;
    /* Each callback owns its samples, even during simultaneous TX and RX. */
    int16_t stereo[PCM_FRAME_SAMPLES * 2];
    int n = len < PCM_FRAME_SAMPLES ? len : PCM_FRAME_SAMPLES;
    if (audio_read_mic(stereo, n * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) return 0;
    for (int i = 0; i < n; ++i) data[i] = esp_g711a_encode(stereo[i * 2]);
    return n;
}

int _receive_audio(unsigned char *data, int len, void *ctx)
{
    if (!data || len <= 0) return 0;
    if (len == 6 && !strncasecmp((char *)data, "DTMF-", 5)) return 0;
    int consumed = 0;
    /* Do not silently discard the tail of an RTP packet larger than 160 samples. */
    while (consumed < len) {
        int16_t stereo[PCM_FRAME_SAMPLES * 2];
        int n = len - consumed;
        if (n > PCM_FRAME_SAMPLES) n = PCM_FRAME_SAMPLES;
        for (int i = 0; i < n; ++i) {
            int16_t sample = esp_g711a_decode(data[consumed + i]);
            stereo[i * 2] = stereo[i * 2 + 1] = sample;
        }
        if (audio_write_spk(stereo, n * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) return consumed;
        consumed += n;
    }
    return consumed;
}

esp_rtc_handle_t sip_service_start(const char *uri)
{
    if (!uri || !g_codec_dev || !wifi_is_connected()) return NULL;
    char *address = _get_network_ip();
    if (!address) return NULL;
    media_lib_add_default_adapter();
    atomic_store(&s_registered, false);
    atomic_store(&s_in_call, false);
    atomic_store(&s_incoming, false);
    atomic_store(&s_last_event, ESP_RTC_EVENT_UNREGISTERED);
    static esp_rtc_data_cb_t callbacks = {
        .send_audio = _send_audio,
        .receive_audio = _receive_audio,
    };
    // ESP_LOGE(TAG,"ip address = %s " , address);
    esp_rtc_config_t config = {
        .uri = uri,
        .local_addr = address,
        .acodec_type = RTC_ACODEC_G711A,
        /* Keep the protocol's frame-size default: older ADF headers do not
           expose aud_frame_size. Callback buffers remain bounded to 160 samples. */
        .data_cb = &callbacks,
        .event_handler = _esp_sip_event_handler,
        .p2p_mode = false,
    };
    return esp_rtc_service_init(&config);
}

int sip_service_stop(esp_rtc_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    int ret = esp_rtc_service_deinit(handle);
    if (ret == ESP_OK) {
        atomic_store(&s_registered, false);
        atomic_store(&s_in_call, false);
        atomic_store(&s_incoming, false);
        atomic_store(&s_last_event, ESP_RTC_EVENT_UNREGISTERED);
    }
    return ret;
}
