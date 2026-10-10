#pragma once

#include "esp_err.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
// #include "esp_h264_enc_hw_param.h"
#include "camera_manip.h"

/**
 * @brief Initialize the H.264 hardware encoder.
 * @param width  Frame width (e.g., CAMERA_H_RES)
 * @param height Frame height (e.g., CAMERA_V_RES)
 * @param fps    Target frames per second
 * @return ESP_OK on success
 */
esp_err_t h264_encoder_init(uint32_t width, uint32_t height, uint32_t fps);

/**
 * @brief Encode a single frame from a raw YUV420 buffer.
 *
 * @param raw_data      Pointer to YUV420 input (O_UYY_E_VYY layout)
 * @param out_buf       Destination buffer for encoded NAL units
 * @param out_buf_size  Size of out_buf in bytes; frames larger than this
 *                      are dropped rather than overflowing
 * @param out_len       Returns the length of the encoded data
 * @return ESP_OK on success, ESP_FAIL on encode error or oversized frame
 */
esp_err_t h264_encoder_process(const uint8_t *raw_data,
                               uint8_t       *out_buf,
                               size_t         out_buf_size,
                               size_t        *out_len);

/**
 * @brief Deinitialize the H.264 encoder and free resources.
 */
void h264_encoder_deinit(void);

// void h264_encoder_force_idr(void);