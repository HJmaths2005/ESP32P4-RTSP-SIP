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

#include "RTSP_manip.h"
#include "WIFI_manip.h"

static const char *TAG = "RTSP_SERVICE";
extern volatile uint32_t s_jpeg_seq;
static esp_rtsp_mode_t rtsp_mode;
volatile uint32_t packet_count = 0;
static int64_t s_window_start_us = 0;       
uint32_t stream_first_pts; 
int16_t pcm_stereo[PCM_SAMPLES * 2];
extern uint8_t use_h264;
extern SemaphoreHandle_t s_frame_lock;
extern uint32_t jpeg_size;
extern uint8_t *s_jpeg_out_buf;
static StreamBufferHandle_t s_mic_ring = NULL;
static TaskHandle_t         s_mic_task = NULL;
static uint32_t s_last_sent_seq = 0;
static volatile uint32_t audio_function_time = 0;
static volatile uint32_t video_function_time = 0;
static int64_t  s_audio_window = 0;
static uint32_t s_audio_pts = 0;

/*
static void mic_task(void *arg)
{
    int16_t frame[PCM_SAMPLES * 2];   
    while (1) {
        if (audio_read_mic(frame, sizeof(frame)) == 0) {
            xStreamBufferSend(s_mic_ring, frame, sizeof(frame), 0);
        }
    }
}
  */  



static uint32_t get_cur_pts()
{
    uint32_t cur_pts = 0;
    if (stream_first_pts == 0) {
        stream_first_pts = esp_timer_get_time() / 1000;
        cur_pts = 0;
    } else {
        cur_pts = esp_timer_get_time() / 1000 - stream_first_pts;
    }
    return cur_pts;
}

static char *_get_network_ip(void)
{
    static char address[16];
    return wifi_get_ip(address, sizeof(address)) ? address : NULL;
}

static int _esp_rtsp_state_handler(esp_rtsp_state_t state, void *ctx)
{
    ESP_LOGD(TAG, "_esp_rtsp_state_handler state %d", state);
    switch ((int)state) {
        case RTSP_STATE_SETUP:
            ESP_LOGI(TAG, "RTSP_STATE_SETUP");
            break;
        case RTSP_STATE_PLAY:
            stream_first_pts = 0;
            if (rtsp_mode == RTSP_CLIENT_PLAY) {
            } else {
            }
            // h264_encoder_force_idr();   /* <-- add this */
            ESP_LOGI(TAG, "RTSP_STATE_PLAY");
            break;
        case RTSP_STATE_TEARDOWN:
            stream_first_pts = 0;
            if (rtsp_mode == RTSP_CLIENT_PLAY) {
            } else {
            }
            ESP_LOGI(TAG, "RTSP_STATE_TEARDOWN");
            break;
    }
    return 0;
}


static int _send_video(unsigned char *data, unsigned int *len, uint32_t *pts, void *ctx)
{
    if (!data || !len || !pts || !s_frame_lock) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        *len = 0;
        return ESP_FAIL;
    }

    if (!s_jpeg_out_buf || jpeg_size == 0 || s_jpeg_seq == s_last_sent_seq) {
        xSemaphoreGive(s_frame_lock);
        *len = 0;
        return ESP_FAIL;

    }

    size_t n = jpeg_size;
    if (n > *len) {
        
        ESP_LOGW(TAG, "JPEG (%u B) exceeds library buffer (%u B), dropping",
                 (unsigned)n, (unsigned)*len);
        xSemaphoreGive(s_frame_lock);
        *len = 0;
        return ESP_OK;
    }

    /* The producer writes back CPU-written bytes before publishing this frame.
       It is now safe to discard the reader's old cache lines on either core. */
    if (esp_cache_msync(s_jpeg_out_buf, ALIGN_UP(n, 64), ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK) {
        xSemaphoreGive(s_frame_lock);
        *len = 0;
        return ESP_FAIL;
    }
    memcpy(data, s_jpeg_out_buf, n);

    s_last_sent_seq = s_jpeg_seq;
    *len = (unsigned int)n;
    *pts = get_cur_pts();
    xSemaphoreGive(s_frame_lock);
    
    /* ---- per-second instrumentation ---- */
    
    packet_count++;
    int64_t now = esp_timer_get_time();
    if (s_window_start_us == 0) s_window_start_us = now;
    if (now - s_window_start_us >= 1000000) {
        ESP_LOGW(TAG, "RTSP tx: %u frames/s, last JPEG %u bytes (~%u kbps)",
                 (unsigned)packet_count, (unsigned)n,
                 (unsigned)(packet_count * n * 8 / 1000));
        packet_count = 0;
        s_window_start_us = now;
    }
    return ESP_OK;
}


esp_rtsp_handle_t rtsp_service_start(esp_rtsp_mode_t mode, const char *uri)
{
    media_lib_add_default_adapter();

    if (!uri || !wifi_is_connected()) return NULL;
    char *address = _get_network_ip();
    if (!address) return NULL;
    s_last_sent_seq = 0;
    stream_first_pts = 0;
    uint16_t width = CAMERA_H_RES, height = CAMERA_V_RES;
    esp_rtsp_video_info_t vcodec_info = {
        .vcodec = camera_uses_h264() ? RTSP_VCODEC_H264 : RTSP_VCODEC_MJPEG,
        .width = width,
        .height = height,
        .fps = 30,
        .len = JPEG_BUF_SIZE,
    };
    esp_rtsp_data_cb_t data_cb = {
        .send_video = _send_video,
    };
    esp_rtsp_config_t rtsp_config = {
        .uri = uri,
        .mode = mode,
        .ctx = NULL,
        .data_cb = &data_cb,
        .audio_enable = false,
        .video_enable = true,
        .acodec = RTSP_ACODEC_G711A,
        .video_info = &vcodec_info,
        .local_addr = address,
        .local_port = RTSP_SERVER_PORT,
        .stack_size = RTSP_STACK_SZIE,
        .task_prio = RTSP_TASK_PRIO,
        .state = _esp_rtsp_state_handler,
        .trans = RTSP_TRANSPORT_TCP,
    };

    rtsp_mode = mode;
    if (mode == RTSP_CLIENT_PLAY) {
        data_cb.receive_audio = /*_receive_audio*/NULL;
        data_cb.send_audio = NULL;
        data_cb.send_video = NULL;
        rtsp_config.video_enable = false;
        rtsp_config.trans = RTSP_TRANSPORT_TCP;
    }

    if (mode == RTSP_SERVER) {
        rtsp_config.local_port = RTSP_SERVER_PORT;
        return esp_rtsp_server_start(&rtsp_config);
    } else {
        return esp_rtsp_client_start(&rtsp_config);
    }

    return ESP_OK;
}

int rtsp_service_stop(esp_rtsp_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return rtsp_mode == RTSP_SERVER ? esp_rtsp_server_stop(handle)
                                    : esp_rtsp_client_stop(handle);
}
