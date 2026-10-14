#pragma once

#include "common.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "driver/isp.h"
#include "esp_lcd_panel_ops.h"
#include "esp_check.h"
#include "driver/ppa.h"
#include "driver/isp_ccm.h"
#include "driver/isp_demosaic.h"
#include "driver/isp_awb.h"
#include "math.h"
#include "driver/jpeg_encode.h"
#include "driver/jpeg_types.h"
#include "h264_manip.h"



#ifdef __cplusplus
extern "C" {
#endif

/* ---------- OV02C10 hardware constants ---------- */
#define OV02C10_I2C_ADDR            0x36
#define OV02C10_CHIP_ID             0x5602
#define OV02C10_MCLK_HZ             19200000

/* Native sensor array is 1928x1092; sensor outputs 1920x1080 */
#define OV02C10_NATIVE_H_RES        1928
#define OV02C10_NATIVE_V_RES        1092

/* ---- Camera output size (what the CSI/ISP must expect) ---- */
#define CAMERA_H_RES                /*1920*/640        /* sensor output width  */
#define CAMERA_V_RES                /*1080*/480        /* sensor output height */

/* 2-lane MIPI, 800 Mbps/lane */
#define OV02C10_LANE_NUM            2
#define OV02C10_LANE_BITRATE_MBPS   800

/* Double buffering */
#define CAMERA_FB_COUNT             3

/* ---- Panel size (what the LCD scans out) ---- */
#define PANEL_H_RES                 800         /* panel width  */
#define PANEL_V_RES                 1280 /* panel height */

// #define STREAM_H_RES   480
// #define STREAM_V_RES   270



#define JPEG_BUF_SIZE   (640 * 1024)


/* The 2 MB crop buffer is allocated in PSRAM at runtime, not here.
 * Declaring it in the header as a non-extern variable causes every
 * translation unit to have its own 2 MB copy in .bss — this is a bug. */
// extern uint16_t *s_crop_buf;
// extern ppa_client_handle_t s_ppa;
// extern jpeg_encoder_handle_t s_jpeg_enc;
// extern SemaphoreHandle_t     s_frame_lock;
// extern jpeg_encoder_buffer_t s_jpeg_out;

/* ---------- Frame-buffer context ---------- */
typedef struct {
    void        *fb[CAMERA_FB_COUNT];   /* frame buffers          */
    size_t       fb_size;               /* bytes per buffer       */
    volatile int producing;             /* CSI is writing into    */
    volatile int ready;                 /* last completed frame   */
    volatile bool frame_ready;          /* new frame available    */

    //probably we dont need this and can safely remove it
    bool         owns_buffers;          /* true if we allocated   */
} camera_fb_ctx_t;

/* ---------- Public API ---------- */

/**
 * @brief  Initialise the OV02C10 sensor, CSI controller, ISP and frame buffers.
 *
 * @param panel  DPI panel handle obtained from init_lcd().
 *               Pass NULL if you only want capture (no display).
 *
 * @note   Must be called *after* i2c_init() (uses the shared i2c_handle)
 *         and *after* init_lcd() (so the panel exists).
 */
esp_err_t camera_init(esp_lcd_panel_handle_t panel);

/** @brief Start the CSI pipeline and submit the first transaction. */
esp_err_t camera_start(void);

/** @brief Stop and release all camera resources. */
esp_err_t camera_deinit(void);

/**
 * @brief  Push the most recent completed frame to the LCD.
 *         Call this from your main loop (or a dedicated display task).
 */
void camera_display_pump(void);

/** @brief Access the frame-buffer context (e.g. from LVGL). */
camera_fb_ctx_t *camera_get_context(void);
bool camera_uses_h264(void);
bool awb_on_statistics_done(isp_awb_ctlr_t ctlr,
                                   const esp_isp_awb_evt_data_t *edata,
                                   void *user_data);

uint32_t s_gamma_curve(uint32_t x);


/* Complete JPEG only. On ESP_ERR_INVALID_SIZE, out_len is the required capacity. */
esp_err_t camera_get_jpeg(uint8_t *out_buf, size_t out_buf_size, size_t *out_len);

/* Must be called once, from camera_init, after buffers are set up */
esp_err_t camera_jpeg_init(void);
esp_err_t camera_encode_h264(void* data ,size_t len , size_t* out_len);

#ifdef __cplusplus
}
#endif
