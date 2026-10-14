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
#ifndef _RTSP_SERVICE_H
#define _RTSP_SERVICE_H

#include "camera_manip.h"
#include "audio_manip.h"
#include "esp_g711.h"
// #include "esp_g722.h"
// #include "esp_opus.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "media_lib_adapter.h"
#include "media_lib_netif.h"
#include "esp_peripherals.h"
#include "esp_rtsp.h"
#include "string.h"
#include "esp_netif.h"
#include "common.h"
#include "esp_cache.h"
#include "freertos/stream_buffer.h"




#define RTSP_SERVER_PORT            8554
#define RTSP_STACK_SZIE             6*1024
#define RTSP_TASK_PRIO              5
#define RTSP_FRAME_SIZE             FRAMESIZE_VGA

//audio constants
#define PCM_SAMPLES 160

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      Start a rtsp service
 *
 * @param[in] mode          // mode can either be SERVER or CLIENT_PUSH or CLIENT_PLAY
 * 
 * @param[in]  uri          SIP uri "Transport://user:pass@server:port/path"
 *
 * @return
 *     - The rtsp handle if successfully created, NULL on error
 */
esp_rtsp_handle_t rtsp_service_start(esp_rtsp_mode_t mode, const char *uri);

/**
 * @brief      Stop rtsp service
 *
 * @param[in]  esp_rtsp   The rtsp handle
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_FAIL on wrong handle
 */
int rtsp_service_stop(esp_rtsp_handle_t esp_rtsp);

#ifdef __cplusplus
}
#endif

#endif