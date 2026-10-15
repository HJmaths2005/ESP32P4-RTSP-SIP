#include "h264_manip.h"
#include <inttypes.h>

static const char *TAG = "H264_ENCODER";

esp_h264_enc_handle_t s_enc = NULL;
uint8_t *s_out_buf       = NULL;
size_t   s_out_buf_size  = 0;

/* Log the first few frame sizes so buffer tuning is easy. */
static uint32_t s_frame_log_counter = 0;
#define FRAME_LOG_LIMIT 30

esp_err_t h264_encoder_init(uint32_t width, uint32_t height, uint32_t fps)
{
    if (!width || !height || !fps || width != CAMERA_H_RES || height != CAMERA_V_RES) return ESP_ERR_INVALID_ARG;
    if (s_enc) return ESP_OK;

    esp_h264_enc_cfg_hw_t cfg = {0};
    /* Each frame is independent: the latest-frame publisher can drop frames. */
    cfg.gop        = 1;
    cfg.fps        = fps;
    cfg.res.width  = width;
    cfg.res.height = height;
    cfg.rc.bitrate = CONFIG_APP_H264_BITRATE;
    cfg.rc.qp_min  = 20;
    cfg.rc.qp_max  = 40;
    cfg.pic_type   = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    /* 1. Create the handle */
    esp_h264_err_t ret = esp_h264_enc_hw_new(&cfg, &s_enc);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "esp_h264_enc_hw_new failed: %d", ret);
        s_enc = NULL;
        return ESP_FAIL;
    }

    /* 2. Open — allocates internal semaphores, ring buffers, claims HW */
    ret = esp_h264_enc_open(s_enc);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "esp_h264_enc_open failed: %d", ret);
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
        return ESP_FAIL;
    }

    /* 3. Internal scratch buffer for encoded NAL units.
     *    Sized to 2 bytes per pixel, which comfortably holds any single
     *    I-frame at the configured resolution. Must be 64-byte aligned
     *    for cache-sync operations on P4. */
    s_out_buf_size = width * height * 2;
    s_out_buf = heap_caps_aligned_alloc(64, s_out_buf_size, MALLOC_CAP_SPIRAM);
    if (!s_out_buf) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        esp_h264_enc_close(s_enc);
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "H.264 HW Encoder ready: %"PRIu32"x%"PRIu32" @ %"PRIu32"fps (scratch=%u B)",
             width, height, fps, (unsigned)s_out_buf_size);
    return ESP_OK;
}

esp_err_t h264_encoder_process(const uint8_t *raw_data,
                               uint8_t       *out_buf,
                               size_t         out_buf_size,
                               size_t        *out_len)
{
    if (out_len) *out_len = 0;
    if (!s_enc || !raw_data || !out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_h264_enc_in_frame_t in_frame = {
        .raw_data.buffer = (uint8_t *)raw_data,
        .raw_data.len    = CAMERA_H_RES * CAMERA_V_RES * 3 / 2,  /* YUV420 */
        .pts             = esp_timer_get_time() / 1000,
    };
    esp_h264_enc_out_frame_t out_frame = {
        .raw_data.buffer = s_out_buf,
        .raw_data.len    = s_out_buf_size,
    };

    esp_h264_err_t ret = esp_h264_enc_process(s_enc, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Encoding failed: %d", ret);
        return ESP_FAIL;
    }

    /* The encoder tells us how many bytes it produced. Verify it against
     * our own scratch buffer before trusting it. */
    size_t produced = /*out_frame.raw_data.len*/out_frame.length;
    if (!produced || produced > s_out_buf_size) {
        ESP_LOGE(TAG, "Encoder reported %u B > scratch %u B — refusing copy",
                 (unsigned)produced, (unsigned)s_out_buf_size);
        return ESP_FAIL;
    }

    /* Verify it fits the caller's destination buffer. Without this check
     * an oversized frame silently overflows into adjacent heap metadata. */
    if (produced > out_buf_size) {
        ESP_LOGW(TAG, "Frame %u B > dst %u B — dropping (I-frame too large?)",
                 (unsigned)produced, (unsigned)out_buf_size);
        return ESP_FAIL;
    }

    memcpy(out_buf, s_out_buf, produced);
    /*    if (s_frame_log_counter < 3) {
    ESP_LOGW(TAG, "frame[%u] first bytes: %02X %02X %02X %02X | %02X %02X %02X %02X",
             (unsigned)s_frame_log_counter,
             out_buf[0], out_buf[1], out_buf[2], out_buf[3],
                
             out_buf[4], out_buf[5], out_buf[6], out_buf[7]);
}
*/
    if (s_frame_log_counter < 1 && produced >= 32) {
    ESP_LOGW(TAG, "frame[0] first 32 bytes:");
    ESP_LOGW(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
             out_buf[0],  out_buf[1],  out_buf[2],  out_buf[3],
             out_buf[4],  out_buf[5],  out_buf[6],  out_buf[7],
             out_buf[8],  out_buf[9],  out_buf[10], out_buf[11],
             out_buf[12], out_buf[13], out_buf[14], out_buf[15]);
    ESP_LOGW(TAG, "  %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
             out_buf[16], out_buf[17], out_buf[18], out_buf[19],
             out_buf[20], out_buf[21], out_buf[22], out_buf[23],
             out_buf[24], out_buf[25], out_buf[26], out_buf[27],
             out_buf[28], out_buf[29], out_buf[30], out_buf[31]);
}

    *out_len = produced;

    /* Log the first N frames so the destination buffer can be tuned. */
    if (s_frame_log_counter < FRAME_LOG_LIMIT) {
        ESP_LOGI(TAG, "frame[%u] = %u B",
                 (unsigned)s_frame_log_counter, (unsigned)produced);
        s_frame_log_counter++;
    }

    return ESP_OK;
}

void h264_encoder_deinit(void)
{
    if (s_enc) {
        esp_h264_enc_close(s_enc);   /* must be called before del */
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
    }
    if (s_out_buf) {
        free(s_out_buf);
        s_out_buf = NULL;
        s_out_buf_size = 0;
    }
    s_frame_log_counter = 0;
    ESP_LOGI(TAG, "H.264 HW Encoder deinitialized");
}
