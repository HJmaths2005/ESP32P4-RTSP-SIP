#include "camera_manip.h"
#include "i2c_manip.h"
#include "esp_cache.h"
#include "esp_check.h"
#include <string.h>

static uint16_t *s_crop_buf = NULL;
static ppa_client_handle_t s_ppa;
static jpeg_encoder_handle_t s_jpeg_enc = NULL;
SemaphoreHandle_t     s_frame_lock = NULL;   /* now protects s_jpeg_out_buf */
uint8_t *s_jpeg_out_buf  = NULL;
static size_t   s_jpeg_out_size = 0;
uint8_t use_h264 = 0;
volatile uint32_t  jpeg_size = 0;
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
static TaskHandle_t s_pipeline_task   = NULL;
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
            .image_quality = 20,
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
    static bool s_get_new_vb(esp_cam_ctlr_handle_t h,
                            esp_cam_ctlr_trans_t *trans,
                            void *user_data)

    {
        
        camera_fb_ctx_t *ctx = (camera_fb_ctx_t *)user_data;
        trans->buffer = ctx->fb[ctx->producing];
        trans->buflen = ctx->fb_size;
        // s_isr_count ++;
        return false;
    }

/* ISR: minimal work — cache sync, swap index, wake the pipeline task. */
static bool s_trans_finished(esp_cam_ctlr_handle_t h,
                             esp_cam_ctlr_trans_t *trans,
                             void *user_data)
{
    // s_isr_count++;

    if((this_time == 0)&&(last_time == 0)){
        this_time = esp_timer_get_time();
    }else{
        elapsed_time = this_time-last_time;
        last_time = this_time;
        this_time= esp_timer_get_time();
    }
    camera_fb_ctx_t *ctx = (camera_fb_ctx_t *)user_data;

    /*
    esp_cache_msync(ctx->fb[ctx->producing], ctx->fb_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                    */

    ctx->ready     = ctx->producing;
    ctx->producing = (ctx->producing + 1) % CAMERA_FB_COUNT;
    BaseType_t woken = pdFALSE;
    if (s_pipeline_task) {
        vTaskNotifyGiveFromISR(s_pipeline_task, &woken);
        // s_notify_count++;
    }
    if (woken) portYIELD_FROM_ISR();
    return false;
}

/* ============================================================
 *  Pipeline task — Architecture A
 *  Blocks on task notification. Does PPA then JPEG back-to-back.
 *  Publishes the JPEG under s_frame_lock for the RTSP task.
 * ============================================================ */
static void camera_pipeline_task(void *arg)
{
    ESP_LOGW(TAG, "cam_pipe: ENTERED (core=%d)", xPortGetCoreID());
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        s_isr_count ++;
        /* Legacy flag for any LVGL consumer that polls it. */
        // s_cam_ctx.frame_ready = false;

        /* Local snapshot — the ISR may update s_cam_ctx.ready at any time. */
        int ready_idx = s_cam_ctx.ready;

        esp_cache_msync(s_cam_ctx.fb[ready_idx], ALIGN_UP(s_cam_ctx.fb_size, 64),
                ESP_CACHE_MSYNC_FLAG_DIR_M2C);

   
        int64_t t0 = esp_timer_get_time();
        esp_err_t ret;

        /* Invalidate cache so the CPU-side JPEG encoder sees fresh PSRAM. */

        /* ===== Step 1b: AWB update (unchanged logic) ===== */
        if (s_awb_ready && s_awb_patch_num > 100) {
            s_awb_ready = false;

            float gain_r = (float)s_awb_sum_g / (float)s_awb_sum_r;
            float gain_b = (float)s_awb_sum_g / (float)s_awb_sum_b;

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

        /* ===== Step 2: JPEG encode into s_jpeg_out_buf =====
         * We hold s_frame_lock during the encode so the RTSP task
         * cannot read s_jpeg_out_buf mid-write. If the RTSP task is
         * slow, the timeout drops this frame rather than stalling
         * the camera pipeline. */

         int64_t t1 = esp_timer_get_time();
        if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "JPEG lock timeout, dropping frame");
            continue;
        }

        if(!use_h264){
            ret = jpeg_encoder_process(s_jpeg_enc, &enc_cfg,
                                        (const uint8_t *)s_cam_ctx.fb[ready_idx],
                                    //    (const uint8_t *)s_crop_buf,
                                    /*PANEL_H_RES*/CAMERA_H_RES * CAMERA_V_RES /*PANEL_V_RES*/ * 3 ,
                                    s_jpeg_out_buf, s_jpeg_out_size,
                                    &jpeg_size);
            if (ret == ESP_OK) {
                s_jpeg_out_len = jpeg_size;
                s_jpeg_seq++;
            } else {
                ESP_LOGE(TAG, "JPEG encode failed: %s", esp_err_to_name(ret));
            }
        }else{
            size_t h264_len = 0;
            esp_err_t h264_ret = h264_encoder_process(
            (const uint8_t *)s_cam_ctx.fb[ready_idx],
            s_jpeg_out_buf,
            s_jpeg_out_size,      /* <-- new argument */
            &h264_len);
            if (h264_ret == ESP_OK) {
            jpeg_size      = h264_len;
            s_jpeg_out_len = h264_len;
            s_jpeg_seq++;
    } else {
        ESP_LOGE(TAG, "H.264 encode failed: %s", esp_err_to_name(h264_ret));
    }
        }

        xSemaphoreGive(s_frame_lock);
        
        int64_t t2 = esp_timer_get_time();

    /*
    if ((s_jpeg_seq % 15) == 1) {
        ESP_LOGW(TAG, "timing: ppa=%lldus jpeg=%lldus total=%lldus fps=%.1f",
             (t1 - t0), (t2 - t1), (t2 - t0), 1000000.0 / (t2 - t0));
    }
             */
             /*
        if(this_time == 0){
            this_time = esp_timer_get_time();
            last_time = 0 ; 
        }else{
         last_time = this_time;
         this_time = esp_timer_get_time();   
        }
        elapsed_time = (this_time - last_time - 7000)/1000;

        ESP_LOGW(TAG, "time between two encodings = %.3f " , elapsed_time);
        */
    //    ESP_LOGW(TAG, "capture took %d" , elapsed_time);
    }
        

}

/* ============================================================
 *  Public API
 * ============================================================ */
esp_err_t camera_init(esp_lcd_panel_handle_t panel)
{
    if (s_initialized) return ESP_OK;
    s_panel = panel;

    ESP_RETURN_ON_ERROR(ov02c10_sensor_init(), TAG, "sensor init");
    ESP_RETURN_ON_ERROR(camera_jpeg_init(),   TAG, "jpeg init");
    //probably changing it to multiplication by 2
    // s_cam_ctx.fb_size = use_h264? CAMERA_H_RES * CAMERA_V_RES * 3 / 2 :CAMERA_H_RES * CAMERA_V_RES * 3;
    s_cam_ctx.fb_size = use_h264? CAMERA_H_RES * CAMERA_V_RES * 3 / 2: CAMERA_H_RES * CAMERA_V_RES * 3;

    ESP_LOGW(TAG, "fb_size = %zu bytes", s_cam_ctx.fb_size);
    //removing the lcd frame buffer and just allocating two memory buffers 
    void *fb0 = NULL, *fb1 = NULL;
    ESP_LOGI(TAG, "Allocating private PSRAM framebuffers");
    for (int i = 0; i < CAMERA_FB_COUNT; i++) {
        s_cam_ctx.fb[i] = heap_caps_aligned_calloc(
        64, 1, s_cam_ctx.fb_size, MALLOC_CAP_SPIRAM);
        if (!s_cam_ctx.fb[i]) return ESP_ERR_NO_MEM;
    }
    s_cam_ctx.owns_buffers = true;
    s_cam_ctx.producing   = 0;
    s_cam_ctx.ready       = 0;
    s_cam_ctx.frame_ready = false;

    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id                = 0,
        .h_res                  = CAMERA_H_RES,
        .v_res                  = CAMERA_V_RES,
        .lane_bit_rate_mbps     = OV02C10_LANE_BITRATE_MBPS,
        .input_data_color_type  = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = use_h264? CAM_CTLR_COLOR_YUV420:CAM_CTLR_COLOR_RGB888,
        // .output_data_color_type = CAM_CTLR_COLOR_RAW10,
        .data_lane_num          = OV02C10_LANE_NUM,
        .byte_swap_en           = false,
        .queue_items            = CAMERA_FB_COUNT,
    };
    ESP_RETURN_ON_ERROR(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle), TAG, "new csi");

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = s_get_new_vb,
        .on_trans_finished = s_trans_finished,
        // .on_error = s_cam_error,
    };
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, &s_cam_ctx),
        TAG, "register cbs");

    ESP_RETURN_ON_ERROR(esp_cam_ctlr_enable(s_cam_handle), TAG, "enable ctlr");

    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz                 = 80 * 1000 * 1000,
        .input_data_source      = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type  = ISP_COLOR_RAW10,
        .output_data_color_type = use_h264? ISP_COLOR_YUV420 : ISP_COLOR_RGB888,
        // .output_data_color_type = ISP_COLOR_YUV422,
        .has_line_start_packet  = false,
        .has_line_end_packet    = false,
        .h_res                  = CAMERA_H_RES,
        .v_res                  = CAMERA_V_RES,
        .bayer_order            = COLOR_RAW_ELEMENT_ORDER_GBRG,
    };
    ESP_RETURN_ON_ERROR(esp_isp_new_processor(&isp_cfg, &s_isp_proc), TAG, "new isp");
    ESP_RETURN_ON_ERROR(esp_isp_enable(s_isp_proc), TAG, "enable isp");

   esp_isp_lsc_gain_array_t gain_array = {};
size_t gain_size = 0;
ESP_ERROR_CHECK(esp_isp_lsc_allocate_gain_array(s_isp_proc, &gain_array, &gain_size));

esp_isp_lsc_config_t lsc_config = {
    .gain_array = &gain_array,
};
isp_lsc_gain_t gain_val = {
    .decimal = 204,
    .integer = 0,
};
for (int i = 0; i < gain_size; i++) {
    gain_array.gain_r[i].val = gain_val.val;
    gain_array.gain_gr[i].val = gain_val.val;
    gain_array.gain_gb[i].val = gain_val.val;
    gain_array.gain_b[i].val = gain_val.val;
}
ESP_ERROR_CHECK(esp_isp_lsc_configure(s_isp_proc, &lsc_config));

    esp_isp_demosaic_config_t demosaic_cfg= {
        .grad_ratio = {
            .integer = 2,
            .decimal = 5,
        },
        .padding_mode = 0,
    };
    ESP_RETURN_ON_ERROR(esp_isp_demosaic_configure(s_isp_proc,&demosaic_cfg),TAG,"isp demosaicing configuration");
    ESP_RETURN_ON_ERROR(esp_isp_demosaic_enable(s_isp_proc), TAG , "isp demosaicing enable");
    ESP_LOGE(TAG,"demosaicing configuration completed");

/* 1. AWB configuration */
esp_isp_awb_config_t awb_cfg = {
    .sample_point = use_h264? ISP_AWB_SAMPLE_POINT_AFTER_CCM :ISP_AWB_SAMPLE_POINT_BEFORE_CCM,
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
ESP_RETURN_ON_ERROR(esp_isp_new_awb_controller(s_isp_proc, &awb_cfg, &s_awb_ctlr),
                    TAG, "new awb ctlr");

/* 3. Register the statistics callback */
esp_isp_awb_cbs_t awb_cbs = {
    .on_statistics_done = awb_on_statistics_done,
};
ESP_RETURN_ON_ERROR(esp_isp_awb_register_event_callbacks(s_awb_ctlr, &awb_cbs, NULL),
                    TAG, "awb register cbs");

/* 4. Enable and start continuous statistics */
ESP_RETURN_ON_ERROR(esp_isp_awb_controller_enable(s_awb_ctlr), TAG, "awb enable");
ESP_RETURN_ON_ERROR(esp_isp_awb_controller_start_continuous_statistics(s_awb_ctlr),
                    TAG, "awb start");


                    esp_isp_ccm_config_t ccm_init = {
    .matrix = {
        { s_ccm_base[0][0], s_ccm_base[0][1], s_ccm_base[0][2] },
        { s_ccm_base[1][0], s_ccm_base[1][1], s_ccm_base[1][2] },
        { s_ccm_base[2][0], s_ccm_base[2][1], s_ccm_base[2][2] },
    },
    .saturation = true,
    .flags = { .update_once_configured = 1 },
};
ESP_ERROR_CHECK(esp_isp_ccm_configure(s_isp_proc, &ccm_init));
ESP_ERROR_CHECK(esp_isp_ccm_enable(s_isp_proc));



isp_gamma_curve_points_t pts = {};
ESP_ERROR_CHECK(esp_isp_gamma_fill_curve_points(s_gamma_curve, &pts));
ESP_ERROR_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_R, &pts));
ESP_ERROR_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_G, &pts));
ESP_ERROR_CHECK(esp_isp_gamma_configure(s_isp_proc, COLOR_COMPONENT_B, &pts));

// Enable gamma module after curve parameters configured
ESP_ERROR_CHECK(esp_isp_gamma_enable(s_isp_proc));



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
        return ESP_ERR_NO_MEM;
    }
        ESP_LOGE(TAG, "created pipeline task");
/*
    enc_cfg = {
            .src_type      = JPEG_ENCODE_IN_FORMAT_RGB888,
            // .sub_sample    = JPEG_DOWN_SAMPLING_YUV422,
            .image_quality = 30,
            .width         = CAMERA_H_RES,
            .height        = CAMERA_V_RES,
        };
        */
    s_initialized = true;
    return ESP_OK;
}

esp_err_t camera_start(void)
{
        if (!s_initialized) return ESP_ERR_INVALID_STATE;

        ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_cam_handle), TAG, "start ctlr");
/*
        esp_cam_ctlr_trans_t first = {
            .buffer = s_cam_ctx.fb[0],
            .buflen = s_cam_ctx.fb_size,
        };
        ESP_RETURN_ON_ERROR(
            esp_cam_ctlr_receive(s_cam_handle, &first, ESP_CAM_CTLR_MAX_DELAY),
            TAG, "receive");

        ESP_LOGI(TAG, "Camera streaming");
        */
        return ESP_OK;
        
    }

void camera_deinit(void) { /* leave as-is */ }

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

bool IRAM_ATTR awb_on_statistics_done(isp_awb_ctlr_t ctlr,
                            const esp_isp_awb_evt_data_t *edata,
                            void *user_data)
{
    const isp_awb_stat_result_t *r = &edata->awb_result;
    s_awb_sum_r     = r->sum_r;
    s_awb_sum_g     = r->sum_g;
    s_awb_sum_b     = r->sum_b;
    s_awb_patch_num = r->white_patch_num;
    s_awb_ready     = true;
    return false;
}

uint32_t s_gamma_curve(uint32_t x)
{
    return pow((double)x / 256, 0.8) * 256;
}

esp_err_t camera_jpeg_init(void)
{
    s_frame_lock = xSemaphoreCreateMutex();
    if (!s_frame_lock) return ESP_ERR_NO_MEM;

    jpeg_encode_engine_cfg_t enc_cfg = {
        .intr_priority = 0,
        .timeout_ms    = 100,
    };
    ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&enc_cfg, &s_jpeg_enc),
                        TAG, "jpeg engine");

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
    if (!s_jpeg_out_buf || !s_frame_lock) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_jpeg_seq == 0 || s_jpeg_out_len == 0) {
        xSemaphoreGive(s_frame_lock);
        return ESP_ERR_INVALID_STATE;   /* no frame produced yet */
    }

    size_t n = s_jpeg_out_len;
    if (n > out_buf_size) n = out_buf_size;

    memcpy(out_buf, s_jpeg_out_buf, n);

    xSemaphoreGive(s_frame_lock);

    *out_len = n;
    return ESP_OK;
}