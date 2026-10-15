#include "camera_manip.h"
#include "i2c_manip.h"
#include "esp_cache.h"
#include "esp_check.h"
#include <string.h>
#include "frame_pool.h"

static jpeg_encoder_handle_t s_jpeg_enc = NULL;
SemaphoreHandle_t     s_frame_lock = NULL;   /* now protects s_jpeg_out_buf */
uint8_t *s_jpeg_out_buf  = NULL;
static size_t   s_jpeg_out_size = 0;
#ifdef CONFIG_APP_USE_H264
uint8_t use_h264 = 1;
#else
uint8_t use_h264 = 0;
#endif
uint32_t jpeg_size = 0;
volatile uint32_t s_isr_count   = 0;
volatile uint32_t this_time  = 0;
volatile uint32_t last_time = 0;
volatile uint32_t elapsed_time = 0;
volatile uint32_t capture_time = 0;
// static olatile uint32_t s_notify_count = 0;
static const char *TAG = "camera_manip";



/* ============================================================
 *  Architecture A state
 * ============================================================ */
static TaskHandle_t s_pipeline_task = NULL;
static SemaphoreHandle_t s_pipeline_done;
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;
static frame_pool_t s_pool;
static bool s_stopping;
static bool s_started;
static bool s_csi_enabled;
static bool s_isp_enabled;
static bool s_awb_enabled;
static bool s_awb_started;
static bool s_demosaic_enabled;
static bool s_ccm_enabled;
static bool s_gamma_enabled;
static bool s_encoding_h264;
static esp_isp_lsc_gain_array_t s_lsc_gains;

static size_t       s_jpeg_out_len    = 0;
volatile uint32_t s_jpeg_seq   = 0;   /* increments on each published frame */

/* ============================================================
 *  OV02C10 registers (unchanged)
 * ============================================================ */
#define OV02C10_REG_CHIP_ID         0x300A
#define OV02C10_REG_SW_RESET        0x0103
#define OV02C10_REG_STREAM_CONTROL  0x0100

/* ... ov02c10_init_seq[] unchanged ... */
static const struct { uint16_t reg; uint8_t val; } ov02c10_init_seq[] = {

    {0x0103, 0x01},  /* software reset */
    {0x0301, 0x08},
    {0x0303, /*0x06*/0x05},  /* PLL1 pre-divider — CRITICAL */
    {0x0304, 0x01},
    {0x0305, 0x90},
    {0x0313, 0x40},
    {0x0316, 0x90},


    {0x031c, 0x4f},
    {0x3016, 0x32},  /* 2-lane MIPI mode */
    {0x301b, 0xf0},
    {0x3020, 0x97},  /* clock selection — CRITICAL for PLL lock */
    {0x3021, 0x23},
    {0x3022, 0x01},
    {0x3026, 0xb4},
    {0x3027, 0xf1},
    {0x303b, 0x00},
    {0x303c, 0x4f},
    {0x303d, 0xe6},
    {0x303e, 0x00},
    {0x303f, 0x03},
    {0x3501, 0x04}, {0x3502, 0x6c},
    {0x3503 ,0x00},
    {0x3504, 0x0c}, {0x3507, 0x00},
    {0x3508, 0x08}, {0x3509, 0x00},
    {0x350a, 0x01}, {0x350b, 0x00},
    {0x350c, 0x41},
    {0x3600, 0x84}, {0x3603, 0x08},
    {0x3610, 0x57}, {0x3611, 0x1b},
    {0x3613, 0x78}, {0x3623, 0x00},
    {0x3632, 0xa0}, {0x3642, 0xe8},
    {0x364c, 0x70}, {0x365d, 0x00},
    {0x365f, 0x0f},
    {0x3708, 0x30}, {0x3714, 0x24},
    {0x3725, 0x02}, {0x3737, 0x08},
    {0x3739, 0x28}, {0x3749, 0x32},
    {0x374a, 0x32}, {0x374b, 0x32},
    {0x374c, 0x32}, {0x374d, 0x81},
    {0x374e, 0x81}, {0x374f, 0x81},
    {0x3752, 0x36}, {0x3753, 0x36},
    {0x3754, 0x36}, {0x3761, 0x00},
    {0x376c, 0x81}, {0x3774, 0x18},
    {0x3776, 0x08}, {0x377c, 0x81},
    {0x377d, 0x81}, {0x377e, 0x81},
    {0x37a0, 0x44}, {0x37a6, 0x44},
    {0x37aa, 0x0d}, {0x37ae, 0x00},
    {0x37cb, 0x03}, {0x37cc, 0x01},
    {0x37d8, 0x02}, {0x37d9, 0x10},
    {0x37e1, 0x10}, {0x37e2, 0x18},
    {0x37e3, 0x08}, {0x37e4, 0x08},
    {0x37e5, 0x02}, {0x37e6, 0x08},
    {0x3800, /*0x00*/0x02}, {0x3801, 0x84/* 0x00*/},
    {0x3802, /*0x00*/0x01}, {0x3803, /*0x04*/0x34},
    {0x3804, /*0x07*/0x05}, {0x3805, 0x0b/*0x8f*/},
    {0x3806, /*0x04*/0x03}, {0x3807, /*0x43*/0x17},
    {0x3808, /*0x07*/0x02}, {0x3809, 0x80},
    {0x380a, /*0x04*/0x01}, {0x380b, /*0x38*/0xe0},
    {0x380c, 0x08/*0x06*/}, {0x380d, 0xe8/*0x40*/},
    {0x380e, 0x04/*0x0b*/}, {0x380f, /*0x8c*/0x92},
    {0x3810, 0x00}, {0x3811, 0x07},
    {0x3812, 0x00}, {0x3813, 0x04},
    {0x3814, 0x01/*0x02*/}, {0x3815, 0x01/*0x02*/},
    {0x3816, 0x01/*0x02*/}, {0x3817, 0x01/*0x02*/},
    {0x3820, 0xa0/*0xa3*/}, {0x3821, 0x00},
    {0x3822, 0x80}, {0x3823, 0x08},
    {0x3824, 0x00}, {0x3825, 0x20},
    {0x3826, 0x00}, {0x3827, 0x08},
    {0x382a, 0x00}, {0x382b, 0x08},
    {0x382d, 0x00}, {0x382e, 0x00},
    {0x382f, 0x23},
    {0x3834, 0x00}, {0x3839, 0x00},
    {0x383a, 0xd1}, {0x383e, 0x03},
    {0x393d, 0x29}, {0x393f, 0x6e},
    {0x394b, 0x06}, {0x394c, 0x06},
    {0x394d, 0x08}, {0x394e, 0x0a},
    {0x394f, 0x01}, {0x3950, 0x01},
    {0x3951, 0x01}, {0x3952, 0x01},
    {0x3953, 0x01}, {0x3954, 0x01},
    {0x3955, 0x01}, {0x3956, 0x01},
    {0x3957, 0x0e}, {0x3958, 0x08},
    {0x3959, 0x08}, {0x395a, 0x08},
    {0x395b, 0x13}, {0x395c, 0x09},
    {0x395d, 0x05}, {0x395e, 0x02},
    {0x395f, 0x00}, {0x3960, 0x00},
    {0x3961, 0x00}, {0x3962, 0x00},
    {0x3963, 0x00}, {0x3964, 0x00},
    {0x3965, 0x00}, {0x3966, 0x00},
    {0x3967, 0x00}, {0x3968, 0x01},
    {0x3969, 0x01}, {0x396a, 0x01},
    {0x396b, 0x01}, {0x396c, 0x10},
    {0x396d, 0xf0}, {0x396e, 0x11},
    {0x396f, 0x00}, {0x3970, 0x37},
    {0x3971, 0x37}, {0x3972, 0x37},
    {0x3973, 0x37}, {0x3974, 0x00},
    {0x3975, 0x3c}, {0x3976, 0x3c},
    {0x3977, 0x3c}, {0x3978, 0x3c},
    {0x3c00, 0x0f}, {0x3c20, 0x01},
    {0x3c21, 0x08},
    {0x3f00, 0x8b}, {0x3f02, 0x0f},
    {0x4000, 0xc3}, {0x4001, 0xe0},
    {0x4002, 0x00}, {0x4003, 0x40},
    {0x4008, 0x04}, {0x4009, 0x23},
    {0x400a, 0x04}, {0x400b, 0x01},
    {0x4041, 0x20},
    {0x4077, 0x06}, {0x4078, 0x00},
    {0x4079, 0x1a}, {0x407a, 0x7f},
    {0x407b, 0x01},
    {0x4080, 0x03}, {0x4081, 0x84},
    {0x4308, 0x03}, {0x4309, 0xff},
    {0x430d, 0x00},
    {0x4500, 0x07}, {0x4501, 0x00/*0x40*/},
    {0x4503, 0x00}, {0x450a, 0x04},
    {0x450e, 0x00}, {0x450f, 0x00},
    {0x4800, 0x64},  /* MIPI CTRL00 */
    {0x4806, 0x00}, {0x4813, 0x00},
    {0x4815, 0x40}, {0x4816, 0x12},
    {0x481f, 0x30},
    {0x4837, 0x14}, {0x4857, 0x05},
    {0x4884, 0x04},
    {0x4900, 0x00}, {0x4901, 0x00},
    {0x4902, 0x01},
    {0x4d00, 0x03}, {0x4d01, 0xd8},
    {0x4d02, 0xba}, {0x4d03, 0xa0},
    {0x4d04, 0xb7}, {0x4d05, 0x34},
    {0x4d0d, 0x00},
    {0x5000, 0xfd/*0xf9*/}, {0x5001, 0x50},
    {0x5006, 0x00}, {0x5080, 0x40},
    {0x5181, 0x2b},
    {0x5202, 0xa3}, {0x5206, 0x01},
    {0x5207, 0x00}, {0x520a, 0x01},
    {0x520b, 0x00},
    {0x5100, 0x01}, {0x5101, 0x00},
    {0x5102, 0x01}, {0x5103, 0x00},
    {0x5104, 0x01}, {0x5105, 0x00},
    {0x4f00, 0x01},
    // {0x030A, 0x10},   /* PLL1_CTRL11: enable lock detection */
    {0x0100, 0x01},   /* streaming on (moved from separate call) */
};
static jpeg_encode_cfg_t enc_cfg = {
            .src_type      = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample    = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = CONFIG_APP_JPEG_QUALITY,
            .width         = CAMERA_H_RES,
            .height        = CAMERA_V_RES,
        };
static i2c_master_dev_handle_t s_ov02c10_dev = NULL;
static esp_cam_ctlr_handle_t   s_cam_handle   = NULL;
static isp_proc_handle_t       s_isp_proc     = NULL;
static esp_lcd_panel_handle_t  s_panel        = NULL;
static camera_fb_ctx_t         s_cam_ctx      = {0};
static bool                    s_initialized  = false;
static isp_awb_ctlr_t          s_awb_ctlr     = NULL;
static volatile uint32_t s_awb_sum_r = 0;
static volatile uint32_t s_awb_sum_g = 0;
static volatile uint32_t s_awb_sum_b = 0;
static volatile uint32_t s_awb_patch_num = 0;
static volatile bool     s_awb_ready = false;
static const float s_ccm_base[3][3] = {
    { 1.10f, -0.05f, -0.05f },
    {-0.05f,  1.00f, -0.05f },
    {-0.05f, -0.05f,  1.10f },
};

/* ============================================================
 *  I2C helpers (unchanged)
 * ============================================================ */
static esp_err_t ov02c10_wr(uint16_t reg, uint8_t val)
{
    uint8_t buf[3] = { (reg >> 8) & 0xFF, reg & 0xFF, val };
    return i2c_master_transmit(s_ov02c10_dev, buf, sizeof(buf), 100);
}

static esp_err_t ov02c10_rd(uint16_t reg, uint8_t *val)
{
    uint8_t addr[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    return i2c_master_transmit_receive(s_ov02c10_dev, addr, 2, val, 1, 100);
}

static void ov02c10_dump_state(void)
{
    ESP_LOGW(TAG, "=== OV02C10 REGISTER DUMP START ===");
    struct { uint16_t reg; const char *name; } regs[] = {
        {0x0100, "MODE_SELECT"},
        {0x031E, "MIPI_BIT_CTRL"},
        {0x3022, "MIPI_SC_CTRL1"},
        {0x3016, "MIPI_SC_CTRL"},
        {0x0300, "PLL1_CTRL1"},
        {0x0301, "PLL1_CTRL2"},
        {0x0302, "PLL1_CTRL3"},
        {0x0303, "PLL1_CTRL4"},
        {0x0304, "PLL1_CTRL5"},
        {0x0305, "PLL1_CTRL6"},
        {0x030A, "PLL1_LOCK"},
        {0x4800, "MIPI_CTRL00"},
        {0x3808, "X_OUT_hi"},
        {0x3809, "X_OUT_lo"},
        {0x380A, "Y_OUT_hi"},
        {0x380B, "Y_OUT_lo"},
        {0x380C, "HTS_hi"},
        {0x380D, "HTS_lo"},
        {0x380E, "VTS_hi"},
        {0x380F, "VTS_lo"},
    };
    for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        uint8_t v = 0xFF;
        esp_err_t r = ov02c10_rd(regs[i].reg, &v);
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "  0x%04X %-14s READ FAILED (%d)",
                     regs[i].reg, regs[i].name, r);
        } else {
            ESP_LOGW(TAG, "  0x%04X %-14s = 0x%02X",
                     regs[i].reg, regs[i].name, v);
        }
    }
    ESP_LOGW(TAG, "=== OV02C10 REGISTER DUMP END ===");
}

static esp_err_t ov02c10_sensor_init(void)
{
    ESP_LOGI(TAG, "Initialising OV02C10 ...");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OV02C10_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(i2c_handle, &dev_cfg, &s_ov02c10_dev),
        TAG, "add i2c device");

    uint8_t id_h = 0, id_l = 0;
    ov02c10_rd(OV02C10_REG_CHIP_ID,     &id_h);
    ov02c10_rd(OV02C10_REG_CHIP_ID + 1, &id_l);
    ESP_LOGI(TAG, "OV02C10 chip ID = 0x%02X%02X", id_h, id_l);

    ov02c10_wr(OV02C10_REG_SW_RESET, 0x01);
    vTaskDelay(pdMS_TO_TICKS(20));

    for (size_t i = 0; i < sizeof(ov02c10_init_seq) / sizeof(ov02c10_init_seq[0]); i++) {
        ESP_RETURN_ON_ERROR(
            ov02c10_wr(ov02c10_init_seq[i].reg, ov02c10_init_seq[i].val),
            TAG, "init seq reg 0x%04X", ov02c10_init_seq[i].reg);
    }

    ESP_RETURN_ON_ERROR(ov02c10_wr(OV02C10_REG_STREAM_CONTROL, 0x01),
                        TAG, "stream on");

    /* ===== NEW ===== Give PLL time to lock, then dump registers */
    ESP_LOGW(TAG, "Streaming command sent, waiting 500 ms...");
    vTaskDelay(pdMS_TO_TICKS(500));
    ov02c10_dump_state();

    ESP_LOGI(TAG, "OV02C10 ready (%dx%d @ 30 fps, 2-lane)",
             CAMERA_H_RES, CAMERA_V_RES);
    return ESP_OK;
}

/* ============================================================
 *  CSI callbacks
 * ============================================================ */
static bool IRAM_ATTR s_get_new_vb(esp_cam_ctlr_handle_t h,
                                        esp_cam_ctlr_trans_t *trans, void *user_data)
{
    camera_fb_ctx_t *ctx = user_data;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    int index = s_stopping ? -1 : frame_pool_capture(&s_pool);
    ctx->producing = index;
    trans->buffer = index >= 0 ? ctx->fb[index] : NULL;
    trans->buflen = index >= 0 ? ctx->fb_size : 0;
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return false;
}

static bool IRAM_ATTR s_trans_finished(esp_cam_ctlr_handle_t h,
                                      esp_cam_ctlr_trans_t *trans, void *user_data)
{
    camera_fb_ctx_t *ctx = user_data;
    BaseType_t woken = pdFALSE;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    for (int i = 0; !s_stopping && i < CAMERA_FB_COUNT; ++i) {
        /* CSI asks for the NEXT buffer before reporting this completed buffer. */
        if (trans->buffer == ctx->fb[i] && frame_pool_complete(&s_pool, i)) {
            ctx->ready = i;
            s_isr_count++;
            if (s_pipeline_task) vTaskNotifyGiveFromISR(s_pipeline_task, &woken);
            break;
        }
    }
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return woken == pdTRUE;
}

static void camera_pipeline_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_capture_lock);
        bool stopping = s_stopping;
        int ready_idx = stopping ? -1 : frame_pool_take(&s_pool);
        bool awb_ready = s_awb_ready;
        uint32_t patch_count = s_awb_patch_num;
        uint32_t sum_r = s_awb_sum_r, sum_g = s_awb_sum_g, sum_b = s_awb_sum_b;
        s_awb_ready = false;
        portEXIT_CRITICAL(&s_capture_lock);
        if (stopping) break;
        if (ready_idx < 0) continue;
        esp_err_t ret = esp_cache_msync(s_cam_ctx.fb[ready_idx], s_cam_ctx.fb_size,
                                        ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if (ret != ESP_OK) goto release_input;
        /* ===== Step 1b: AWB update (unchanged logic) ===== */
        if (awb_ready && patch_count > 100 && sum_r && sum_b) {

            float gain_r = (float)sum_g / (float)sum_r;
            float gain_b = (float)sum_g / (float)sum_b;

            if (gain_r < 0.5f) gain_r = 0.5f;
            if (gain_r > 3.0f) gain_r = 3.0f;
            if (gain_b < 0.5f) gain_b = 0.5f;
            if (gain_b > 3.0f) gain_b = 3.0f;

            static float s_gain_r_smooth = 1.0f;
            static float s_gain_b_smooth = 1.0f;
            s_gain_r_smooth = 0.8f * s_gain_r_smooth + 0.2f * gain_r;
            s_gain_b_smooth = 0.8f * s_gain_b_smooth + 0.2f * gain_b;

            esp_isp_ccm_config_t ccm = {
                .matrix = {
                    { s_ccm_base[0][0] * s_gain_r_smooth,
                      s_ccm_base[0][1],
                      s_ccm_base[0][2] * s_gain_b_smooth },
                    { s_ccm_base[1][0] * s_gain_r_smooth,
                      s_ccm_base[1][1],
                      s_ccm_base[1][2] * s_gain_b_smooth },
                    { s_ccm_base[2][0] * s_gain_r_smooth,
                      s_ccm_base[2][1],
                      s_ccm_base[2][2] * s_gain_b_smooth },
                },
                .saturation = true,
                .flags = { .update_once_configured = 0 },
            };
            esp_isp_ccm_configure(s_isp_proc, &ccm);
        }


        if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(100)) != pdTRUE) goto release_input;
        /* A failed encoder can overwrite its destination; do not publish old lengths. */
        jpeg_size = 0;
        s_jpeg_out_len = 0;
        if (!s_encoding_h264) {
            uint32_t encoded_length = 0;
            ret = jpeg_encoder_process(s_jpeg_enc, &enc_cfg,
                    s_cam_ctx.fb[ready_idx], s_cam_ctx.fb_size,
                    s_jpeg_out_buf, s_jpeg_out_size, &encoded_length);
            if (ret == ESP_OK) jpeg_size = encoded_length;
        } else {
            size_t encoded_length = 0;
            ret = h264_encoder_process(s_cam_ctx.fb[ready_idx], s_jpeg_out_buf,
                                        s_jpeg_out_size, &encoded_length);
            if (ret == ESP_OK) jpeg_size = encoded_length;
        }
        if (ret == ESP_OK && jpeg_size) {
            /* Publish CPU-written headers/H.264 bytes to shared memory on both cores. */
            ret = esp_cache_msync(s_jpeg_out_buf, ALIGN_UP(jpeg_size, 64),
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (ret == ESP_OK) { s_jpeg_out_len = jpeg_size; ++s_jpeg_seq; }
        }
        if (ret != ESP_OK) { jpeg_size = 0; ESP_LOGW(TAG, "Encode failed: %s", esp_err_to_name(ret)); }
        xSemaphoreGive(s_frame_lock);
release_input:
        portENTER_CRITICAL(&s_capture_lock);
        frame_pool_release(&s_pool, ready_idx);
        portEXIT_CRITICAL(&s_capture_lock);
    }
    xSemaphoreGive(s_pipeline_done);
    vTaskDelete(NULL);
}

/* ============================================================
 *  Public API
 * ============================================================ */
esp_err_t camera_init(esp_lcd_panel_handle_t panel)
{
    if (s_initialized) return s_stopping ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (s_pipeline_done || s_cam_handle || s_isp_proc || s_ov02c10_dev) return ESP_ERR_INVALID_STATE;
#define CAMERA_INIT_CHECK(call) ESP_GOTO_ON_ERROR((call), fail, TAG, "camera init")
    esp_err_t ret = ESP_OK;
    s_panel = panel;
    s_encoding_h264 = use_h264 != 0;
    frame_pool_init(&s_pool);
    s_stopping = false;
    s_pipeline_done = xSemaphoreCreateBinary();
    if (!s_pipeline_done) return ESP_ERR_NO_MEM;

    ESP_GOTO_ON_ERROR(ov02c10_sensor_init(), fail, TAG, "sensor init");
    ESP_GOTO_ON_ERROR(camera_jpeg_init(), fail, TAG, "jpeg init");
    //probably changing it to multiplication by 2
    // s_cam_ctx.fb_size = s_encoding_h264 ? CAMERA_H_RES * CAMERA_V_RES * 3 / 2 :CAMERA_H_RES * CAMERA_V_RES * 3;
    s_cam_ctx.fb_size = s_encoding_h264 ? CAMERA_H_RES * CAMERA_V_RES * 3 / 2: CAMERA_H_RES * CAMERA_V_RES * 3;

    ESP_LOGW(TAG, "fb_size = %zu bytes", s_cam_ctx.fb_size);
    //removing the lcd frame buffer and just allocating two memory buffers 
    ESP_LOGI(TAG, "Allocating private PSRAM framebuffers");
    for (int i = 0; i < CAMERA_FB_COUNT; i++) {
        s_cam_ctx.fb[i] = heap_caps_aligned_calloc(
        64, 1, s_cam_ctx.fb_size, MALLOC_CAP_SPIRAM);
        if (!s_cam_ctx.fb[i]) { ret = ESP_ERR_NO_MEM; goto fail; }
    }
    s_cam_ctx.owns_buffers = true;
    s_cam_ctx.producing   = 0;
    s_cam_ctx.ready       = -1;
    s_cam_ctx.frame_ready = false;

    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id                = 0,
        .h_res                  = CAMERA_H_RES,
        .v_res                  = CAMERA_V_RES,
        .lane_bit_rate_mbps     = OV02C10_LANE_BITRATE_MBPS,
        .input_data_color_type  = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = s_encoding_h264 ? CAM_CTLR_COLOR_YUV420:CAM_CTLR_COLOR_RGB888,
        // .output_data_color_type = CAM_CTLR_COLOR_RAW10,
        .data_lane_num          = OV02C10_LANE_NUM,
        .byte_swap_en           = false,
        .queue_items            = CAMERA_FB_COUNT,
    };
    ESP_GOTO_ON_ERROR(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle), fail, TAG, "new csi");

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = s_get_new_vb,
        .on_trans_finished = s_trans_finished,
        // .on_error = s_cam_error,
    };
    ESP_GOTO_ON_ERROR(
        esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, &s_cam_ctx), fail, TAG, "register cbs");

    ESP_GOTO_ON_ERROR(esp_cam_ctlr_enable(s_cam_handle), fail, TAG, "enable ctlr");

    s_csi_enabled = true;

    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz                 = 80 * 1000 * 1000,
        .input_data_source      = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type  = ISP_COLOR_RAW10,
        .output_data_color_type = s_encoding_h264 ? ISP_COLOR_YUV420 : ISP_COLOR_RGB888,
        // .output_data_color_type = ISP_COLOR_YUV422,
        .has_line_start_packet  = false,
        .has_line_end_packet    = false,
        .h_res                  = CAMERA_H_RES,
        .v_res                  = CAMERA_V_RES,
        .bayer_order            = COLOR_RAW_ELEMENT_ORDER_GBRG,
    };
    ESP_GOTO_ON_ERROR(esp_isp_new_processor(&isp_cfg, &s_isp_proc), fail, TAG, "new isp");
    ESP_GOTO_ON_ERROR(esp_isp_enable(s_isp_proc), fail, TAG, "enable isp");

    s_isp_enabled = true;
    esp_isp_lsc_gain_array_t *gain_array = &s_lsc_gains;
size_t gain_size = 0;
CAMERA_INIT_CHECK(esp_isp_lsc_allocate_gain_array(s_isp_proc, gain_array, &gain_size));

esp_isp_lsc_config_t lsc_config = {
    .gain_array = gain_array,
};
isp_lsc_gain_t gain_val = {
    .decimal = 204,
    .integer = 0,
};
for (int i = 0; i < gain_size; i++) {
    gain_array->gain_r[i].val = gain_val.val;
    gain_array->gain_gr[i].val = gain_val.val;
    gain_array->gain_gb[i].val = gain_val.val;
    gain_array->gain_b[i].val = gain_val.val;
}
CAMERA_INIT_CHECK(esp_isp_lsc_configure(s_isp_proc, &lsc_config));

    esp_isp_demosaic_config_t demosaic_cfg= {
        .grad_ratio = {
            .integer = 2,
            .decimal = 5,
        },
        .padding_mode = 0,
    };
    ESP_GOTO_ON_ERROR(esp_isp_demosaic_configure(s_isp_proc,&demosaic_cfg), fail, TAG,"isp demosaicing configuration");
    ESP_GOTO_ON_ERROR(esp_isp_demosaic_enable(s_isp_proc), fail, TAG, "isp demosaicing enable");
    s_demosaic_enabled = true;

/* 1. AWB configuration */
esp_isp_awb_config_t awb_cfg = {
    .sample_point = s_encoding_h264 ? ISP_AWB_SAMPLE_POINT_AFTER_CCM :ISP_AWB_SAMPLE_POINT_BEFORE_CCM,
    .window = {
        .top_left  = { .x = /*480*/160,  .y = /*270*/120 },
        .btm_right = { .x = /*1440*/480, .y = /*810*/360 },
    },
    .subwindow = {
        .top_left  = { .x = /*480*/160,  .y = /*270*/120 },
        .btm_right = { .x = /*1440*/480, .y = /*810*/360 },
    },
    .white_patch = {
        .luminance = { .min = 0, .max = 700 },
        .red_green_ratio  = { .min = 0.5f, .max = 2.0f },
        .blue_green_ratio = { .min = 0.5f, .max = 1.8f },
    },
    .intr_priority = 0,
};

/* 2. Create the AWB controller */
ESP_GOTO_ON_ERROR(esp_isp_new_awb_controller(s_isp_proc, &awb_cfg, &s_awb_ctlr), fail, TAG, "new awb ctlr");

/* 3. Register the statistics callback */
esp_isp_awb_cbs_t awb_cbs = {
    .on_statistics_done = awb_on_statistics_done,
};
ESP_GOTO_ON_ERROR(esp_isp_awb_register_event_callbacks(s_awb_ctlr, &awb_cbs, NULL), fail, TAG, "awb register cbs");

/* 4. Enable and start continuous statistics */
ESP_GOTO_ON_ERROR(esp_isp_awb_controller_enable(s_awb_ctlr), fail, TAG, "awb enable");
s_awb_enabled = true;
ESP_GOTO_ON_ERROR(esp_isp_awb_controller_start_continuous_statistics(s_awb_ctlr), fail, TAG, "awb start");


s_awb_started = true;

    esp_isp_ccm_config_t ccm_init = {
    .matrix = {
        { s_ccm_base[0][0], s_ccm_base[0][1], s_ccm_base[0][2] },
        { s_ccm_base[1][0], s_ccm_base[1][1], s_ccm_base[1][2] },
        { s_ccm_base[2][0], s_ccm_base[2][1], s_ccm_base[2][2] },
    },
    .saturation = true,
    .flags = { .update_once_configured = 1 },
};
CAMERA_INIT_CHECK(esp_isp_ccm_configure(s_isp_proc, &ccm_init));
CAMERA_INIT_CHECK(esp_isp_ccm_enable(s_isp_proc));
    s_ccm_enabled = true;



isp_gamma_curve_points_t pts = {};
CAMERA_INIT_CHECK(esp_isp_gamma_fill_curve_points(s_gamma_curve, &pts));
CAMERA_INIT_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_R, &pts));
CAMERA_INIT_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_G, &pts));
CAMERA_INIT_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_B, &pts));

// Enable gamma module after curve parameters configured
CAMERA_INIT_CHECK(esp_isp_gamma_enable(s_isp_proc));
    s_gamma_enabled = true;



    /* ===== Create the pipeline task ===== */
    BaseType_t ok = xTaskCreatePinnedToCore(
        camera_pipeline_task,
        "cam_pipe",
        8192,               /* stack: PPA + JPEG + AWB math */
        NULL,
        8,                  /* priority */
        &s_pipeline_task,
        1);                 /* core 1 */
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create pipeline task");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
        ESP_LOGE(TAG, "created pipeline task");
    s_initialized = true;
#undef CAMERA_INIT_CHECK
    return ESP_OK;
fail:
    if (camera_deinit() != ESP_OK) ESP_LOGE(TAG, "Camera rollback incomplete; retry deinit before init");
    return ret;
}

esp_err_t camera_start(void)
{
    if (!s_initialized || s_stopping) return ESP_ERR_INVALID_STATE;
    if (s_started) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_cam_handle), TAG, "start ctlr");
    s_started = true;
    return ESP_OK;
}

esp_err_t camera_deinit(void)
{
    esp_err_t ret;
#define CAMERA_CLEANUP_CHECK(call) do { ret = (call); if (ret != ESP_OK) return ret; } while (0)
    portENTER_CRITICAL(&s_capture_lock);
    s_stopping = true;
    portEXIT_CRITICAL(&s_capture_lock);
    if (s_started) {
        if (esp_cam_ctlr_stop(s_cam_handle) != ESP_OK) {
            ESP_LOGE(TAG, "CSI stop failed; preserving buffers still owned by DMA");
            return ESP_FAIL;
        }
        s_started = false;
    }
    if (s_pipeline_task) {
        xTaskNotifyGive(s_pipeline_task);
        xSemaphoreTake(s_pipeline_done, portMAX_DELAY); /* Let any hardware encode finish. */
        s_pipeline_task = NULL;
    }
    if (s_awb_started) { CAMERA_CLEANUP_CHECK(esp_isp_awb_controller_stop_continuous_statistics(s_awb_ctlr)); s_awb_started = false; }
    if (s_awb_enabled) { CAMERA_CLEANUP_CHECK(esp_isp_awb_controller_disable(s_awb_ctlr)); s_awb_enabled = false; }
    if (s_awb_ctlr) { CAMERA_CLEANUP_CHECK(esp_isp_del_awb_controller(s_awb_ctlr)); s_awb_ctlr = NULL; }
    if (s_demosaic_enabled) { CAMERA_CLEANUP_CHECK(esp_isp_demosaic_disable(s_isp_proc)); s_demosaic_enabled = false; }
    if (s_ccm_enabled) { CAMERA_CLEANUP_CHECK(esp_isp_ccm_disable(s_isp_proc)); s_ccm_enabled = false; }
    if (s_gamma_enabled) { CAMERA_CLEANUP_CHECK(esp_isp_gamma_disable(s_isp_proc)); s_gamma_enabled = false; }
    if (s_isp_enabled) { CAMERA_CLEANUP_CHECK(esp_isp_disable(s_isp_proc)); s_isp_enabled = false; }
    if (s_isp_proc) { CAMERA_CLEANUP_CHECK(esp_isp_del_processor(s_isp_proc)); s_isp_proc = NULL; }
    if (s_csi_enabled) { CAMERA_CLEANUP_CHECK(esp_cam_ctlr_disable(s_cam_handle)); s_csi_enabled = false; }
    if (s_cam_handle) { CAMERA_CLEANUP_CHECK(esp_cam_ctlr_del(s_cam_handle)); s_cam_handle = NULL; }
    if (s_ov02c10_dev) {
        if (ov02c10_wr(OV02C10_REG_STREAM_CONTROL, 0) != ESP_OK) ESP_LOGW(TAG, "Sensor did not acknowledge stream-off; CSI is stopped");
        CAMERA_CLEANUP_CHECK(i2c_master_bus_rm_device(s_ov02c10_dev));
        s_ov02c10_dev = NULL;
    }
    free(s_lsc_gains.gain_r); free(s_lsc_gains.gain_gr);
    free(s_lsc_gains.gain_gb); free(s_lsc_gains.gain_b);
    memset(&s_lsc_gains, 0, sizeof(s_lsc_gains));
    for (int i = 0; i < CAMERA_FB_COUNT; ++i) {
        free(s_cam_ctx.fb[i]);
        s_cam_ctx.fb[i] = NULL;
    }
    if (s_frame_lock) xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    jpeg_size = 0;
    s_jpeg_out_len = 0;
    if (s_jpeg_enc) { jpeg_del_encoder_engine(s_jpeg_enc); s_jpeg_enc = NULL; }
    h264_encoder_deinit();
    free(s_jpeg_out_buf);
    s_jpeg_out_buf = NULL;
    s_jpeg_out_size = 0;
    if (s_frame_lock) xSemaphoreGive(s_frame_lock);
    /* Keep the publication mutex for the application lifetime: RTSP readers may
       still hold its handle. It is reused on the next init, never reallocated. */
    if (s_pipeline_done) { vSemaphoreDelete(s_pipeline_done); s_pipeline_done = NULL; }
    memset(&s_cam_ctx, 0, sizeof(s_cam_ctx));
    frame_pool_init(&s_pool);
    s_awb_ready = false;
    s_initialized = false;
    s_panel = NULL;
#undef CAMERA_CLEANUP_CHECK
    return ESP_OK;
}

/* ============================================================
 *  camera_display_pump — now a no-op stub
 *  The pipeline task does the PPA work. Kept for header ABI.
 * ============================================================ */
void camera_display_pump(void)
{
    /* Intentionally empty. The PPA + JPEG pipeline now runs in
     * camera_pipeline_task, woken by the CSI ISR. Do not call
     * this from a polling loop — it would do nothing. */
}

camera_fb_ctx_t *camera_get_context(void)
{
    return &s_cam_ctx;
}

bool camera_uses_h264(void) { return s_encoding_h264; }

bool IRAM_ATTR awb_on_statistics_done(isp_awb_ctlr_t ctlr,
                            const esp_isp_awb_evt_data_t *edata,
                            void *user_data)
{
    const isp_awb_stat_result_t *r = &edata->awb_result;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    s_awb_sum_r     = r->sum_r;
    s_awb_sum_g     = r->sum_g;
    s_awb_sum_b     = r->sum_b;
    s_awb_patch_num = r->white_patch_num;
    s_awb_ready     = true;
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return false;
}

uint32_t s_gamma_curve(uint32_t x)
{
    return pow((double)x / 256, 0.8) * 256;
}

esp_err_t camera_jpeg_init(void)
{
    if (!s_frame_lock) s_frame_lock = xSemaphoreCreateMutex();
    if (!s_frame_lock) return ESP_ERR_NO_MEM;

    if (s_encoding_h264) {
        ESP_RETURN_ON_ERROR(h264_encoder_init(CAMERA_H_RES, CAMERA_V_RES, 30), TAG, "h264 init");
    }

    jpeg_encode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms    = 100,
    };
    if (!s_encoding_h264) {
        ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&engine_cfg, &s_jpeg_enc), TAG, "jpeg engine");
    }

    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    s_jpeg_out_buf = (uint8_t *)jpeg_alloc_encoder_mem(JPEG_BUF_SIZE,
                                                       &mem_cfg,
                                                       &s_jpeg_out_size);
    if (!s_jpeg_out_buf) {
        ESP_LOGE(TAG, "jpeg out buf alloc failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "JPEG ready, out buffer %zu bytes at %p",
             s_jpeg_out_size, s_jpeg_out_buf);
    return ESP_OK;
}

/* ============================================================
 *  camera_get_jpeg — now a pure consumer
 *  Blocks on the lock, copies the latest published JPEG.
 * ============================================================ */
esp_err_t camera_get_jpeg(uint8_t *out_buf, size_t out_buf_size, size_t *out_len)
{
    if (!out_len || !out_buf) return ESP_ERR_INVALID_ARG;
    *out_len = 0;
    if (!s_frame_lock) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_encoding_h264 || !s_jpeg_out_buf || s_jpeg_seq == 0 || s_jpeg_out_len == 0) {
        xSemaphoreGive(s_frame_lock);
        return ESP_ERR_INVALID_STATE;   /* no frame produced yet */
    }

    size_t n = s_jpeg_out_len;
    if (n > out_buf_size) {
        *out_len = n;
        xSemaphoreGive(s_frame_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t sync_ret = esp_cache_msync(s_jpeg_out_buf, ALIGN_UP(n, 64), ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if (sync_ret != ESP_OK) {
        xSemaphoreGive(s_frame_lock);
        return sync_ret;
    }
    memcpy(out_buf, s_jpeg_out_buf, n);

    xSemaphoreGive(s_frame_lock);

    *out_len = n;
    return ESP_OK;
}

/* Independent consumer cursor: SIP cannot consume an RTSP reader's frame.
 * Nonblocking because esp_rtc invokes video callbacks from its timer task. */
esp_err_t camera_copy_encoded_frame(uint8_t *out_buf, size_t capacity,
                                    size_t *out_len, uint32_t *last_sequence)
{
    if (!out_len) return ESP_ERR_INVALID_ARG;
    *out_len = 0;
    if (!out_buf || !last_sequence) return ESP_ERR_INVALID_ARG;
    if (!s_frame_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_frame_lock, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (s_jpeg_out_buf && s_jpeg_out_len && s_jpeg_seq != *last_sequence) {
        if (s_jpeg_out_len > capacity) {
            ret = ESP_ERR_INVALID_SIZE;
        } else {
            ret = esp_cache_msync(s_jpeg_out_buf, ALIGN_UP(s_jpeg_out_len, 64),
                                 ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (ret == ESP_OK) {
                memcpy(out_buf, s_jpeg_out_buf, s_jpeg_out_len);
                *out_len = s_jpeg_out_len;
                *last_sequence = s_jpeg_seq;
            }
        }
    }
    xSemaphoreGive(s_frame_lock);
    return ret;
}
