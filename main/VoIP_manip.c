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



static const char *TAG = "SIP_SERVICE";

char *_get_network_ip()
{
    media_lib_ipv4_info_t ip_info;
    media_lib_netif_get_ipv4_info(MEDIA_LIB_NET_TYPE_STA, &ip_info);
    return media_lib_ipv4_ntoa(&ip_info.ip);
}

int _esp_sip_event_handler(esp_rtc_event_t event, void *ctx)
{
    // av_stream_handle_t av_stream = (av_stream_handle_t) ctx;
    ESP_LOGD(TAG, "_esp_sip_event_handler event %d", event);
    switch ((int)event) {
        case ESP_RTC_EVENT_REGISTERED:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_REGISTERED");
            // audio_player_int_tone_play(tone_uri[TONE_TYPE_SERVER_CONNECT]);
            break;
        case ESP_RTC_EVENT_UNREGISTERED:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_UNREGISTERED");
            break;
        case ESP_RTC_EVENT_CALLING:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_CALLING Remote Ring...");
            break;
        case ESP_RTC_EVENT_INCOMING:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_INCOMING...");
            // audio_player_int_tone_play(tone_uri[TONE_TYPE_ALARM]);
            break;
        case ESP_RTC_EVENT_AUDIO_SESSION_BEGIN:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_AUDIO_SESSION_BEGIN");
            // av_audio_enc_start(av_stream);
            // av_audio_dec_start(av_stream);
            break;
        case ESP_RTC_EVENT_AUDIO_SESSION_END:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_AUDIO_SESSION_END");
            // av_audio_enc_stop(av_stream);
            // av_audio_dec_stop(av_stream);
            break;
        case ESP_RTC_EVENT_CALL_ANSWERED:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_CALL_ANSWERED");
            break;
        case ESP_RTC_EVENT_HANGUP:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_HANGUP");
            break;
        case ESP_RTC_EVENT_ERROR:
            ESP_LOGI(TAG, "ESP_RTC_EVENT_ERROR");
            break;
    }

    return ESP_OK;
}

int _send_audio(unsigned char *data, int len, void *ctx)
{
   
    int n = len < PCM_FRAME_SAMPLES ? len : PCM_FRAME_SAMPLES;

    if (audio_read_mic(pcm_stereo, n * 2 * sizeof(int16_t)) < 0) {
        return 0;
    }

    for (int i = 0; i < n; i++) {
        pcm_mono[i] = pcm_stereo[i * 2];
        data[i] = esp_g711a_encode(pcm_mono[i]);
    }
    return n;     
}

int _receive_audio(unsigned char *data, int len, void *ctx)
{
    static uint32_t s_rx = 0;
    static int64_t  s_win = 0;
    s_rx++;
    int64_t now = esp_timer_get_time();
    if (s_win == 0) s_win = now;
    if (now - s_win >= 1000000) {
        ESP_LOGW(TAG, "rx_audio %u calls/s", (unsigned)s_rx);
        s_rx = 0;
        s_win = now;
    }


        if ((len == 6) && !strncasecmp((char *)data, "DTMF-", 5)) {
        ESP_LOGI("SIP_SERVICE", "DTMF: %d", data[5]);
        return 0;
    }

    int n = len < PCM_FRAME_SAMPLES ? len : PCM_FRAME_SAMPLES;

    /* 1. G.711A decode to mono PCM, then duplicate to stereo */
    for (int i = 0; i < n; i++) {
        int16_t s = esp_g711a_decode(data[i]);
        pcm_stereo[i * 2]     = s;
        pcm_stereo[i * 2 + 1] = s;
    }
    static uint32_t s_dump = 0;
    if (s_dump < 5) {
        ESP_LOGW(TAG, "rx[%u] len=%d first4=%d %d %d %d",
                 (unsigned)s_dump, len,
                 pcm_stereo[0], pcm_stereo[2],
                 pcm_stereo[4], pcm_stereo[6]);
        s_dump++;
    }
    /* 2. Write to codec */
    int ret = audio_write_spk(pcm_stereo, n * 2 * sizeof(int16_t));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "audio_write_spk failed: %d", ret);
        return n;
    }
    return n;
    
}

esp_rtc_handle_t sip_service_start(const char *uri)
{
    media_lib_add_default_adapter();

    esp_rtc_data_cb_t data_cb = {
        .send_audio = _send_audio,
        .receive_audio = _receive_audio,
    };
    esp_rtc_config_t sip_service_config = {
        .uri = uri,
        // .ctx = av_handler,
        .local_addr = _get_network_ip(),
        .acodec_type = RTC_ACODEC_G711A,
        .data_cb = &data_cb,
        .event_handler = _esp_sip_event_handler,
    };

    return esp_rtc_service_init(&sip_service_config);
}

int sip_service_stop(esp_rtc_handle_t esp_sip)
{
    int ret = ESP_FAIL;
    if (esp_sip) {
        return esp_rtc_service_deinit(esp_sip);
    }
    return ret;
}