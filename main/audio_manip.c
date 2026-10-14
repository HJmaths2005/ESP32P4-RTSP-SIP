#include "audio_manip.h"
#include "i2c_manip.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "audio_manip";

#define I2S_NUM             I2S_NUM_0
#define I2S_MCK_IO          GPIO_NUM_13
#define I2S_BCK_IO          GPIO_NUM_12
#define I2S_WS_IO           GPIO_NUM_10
#define I2S_DO_IO           GPIO_NUM_9
#define I2S_DI_IO           GPIO_NUM_11
// #define EXAMPLE_PA_CTRL_IO  GPIO_NUM_53
#define EXAMPLE_PA_CTRL_IO  GPIO_NUM_20
#define ES8311_ADDR         0x30

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

esp_codec_dev_handle_t g_codec_dev = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;

void audio_codec_deinit(void)
{
    if (g_codec_dev) {
        esp_codec_dev_close(g_codec_dev);
        esp_codec_dev_delete(g_codec_dev);
        g_codec_dev = NULL;
    }
    if (s_codec_if) audio_codec_delete_codec_if(s_codec_if);
    if (s_data_if) audio_codec_delete_data_if(s_data_if);
    if (s_ctrl_if) audio_codec_delete_ctrl_if(s_ctrl_if);
    if (s_gpio_if) audio_codec_delete_gpio_if(s_gpio_if);
    s_codec_if = NULL; s_data_if = NULL; s_ctrl_if = NULL; s_gpio_if = NULL;
    if (tx_handle) { i2s_channel_disable(tx_handle); i2s_del_channel(tx_handle); tx_handle = NULL; }
    if (rx_handle) { i2s_channel_disable(rx_handle); i2s_del_channel(rx_handle); rx_handle = NULL; }
}


static esp_err_t i2s_driver_init(int sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle),
                        TAG, "i2s_new_channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_DI_IO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg),
                        TAG, "init tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &std_cfg),
                        TAG, "init rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "enable tx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "enable rx");
    return ESP_OK;
}

esp_err_t audio_codec_init(int sample_rate)
{
    if (g_codec_dev) return ESP_OK;
    if (sample_rate != 8000) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_FAIL;
    /* 1. I2S */
    ret = i2s_driver_init(sample_rate);
    if (ret != ESP_OK) goto fail;

    ret = ESP_FAIL;
    /* 2. Reuse the I2C bus you already created for touch in i2c_manip.c */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_ADDR,
        .bus_handle = i2c_handle,      /* <-- shared, not new */
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_ctrl_if) { ESP_LOGE(TAG, "ctrl_if alloc failed"); goto fail; }

    /* 3. Data interface */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_data_if) { ESP_LOGE(TAG, "data_if alloc failed"); goto fail; }

    /* 4. GPIO interface for PA */
    s_gpio_if = audio_codec_new_gpio();
    if (!s_gpio_if) { ESP_LOGE(TAG, "gpio_if alloc failed"); goto fail; }

    /* 5. ES8311 codec interface */
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = EXAMPLE_PA_CTRL_IO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    s_codec_if = es8311_codec_new(&es8311_cfg);
    if (!s_codec_if) { ESP_LOGE(TAG, "es8311_if alloc failed"); goto fail; }

    /* 6. Top-level codec device */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    g_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (!g_codec_dev) { ESP_LOGE(TAG, "codec dev alloc failed"); goto fail; }

    /* 7. Open with the requested sample config */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = 16,
        .channel = 2,          /* I2S stereo, codec uses one channel */
        .channel_mask = 0x03,
        .sample_rate = sample_rate,
    };
    if (esp_codec_dev_open(g_codec_dev, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed");
        goto fail;
    }

    esp_codec_dev_set_out_vol(g_codec_dev, 100);
    esp_codec_dev_set_in_gain(g_codec_dev, 20.0);
    esp_codec_dev_set_out_mute(g_codec_dev,false);
    gpio_set_direction(GPIO_NUM_20,GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_20,1);

    ESP_LOGI(TAG, "ES8311 codec ready @ %d Hz", sample_rate);
    return ESP_OK;
fail:
    audio_codec_deinit();
    return ret;
}

/* Helpers used by VoIP_manip.c */
int audio_read_mic(void *data, int len)
{
    if (!g_codec_dev || !data || len <= 0) return ESP_CODEC_DEV_INVALID_ARG;
    return esp_codec_dev_read(g_codec_dev, data, len);
}

int audio_write_spk(const void *data, int len)
{
    if (!g_codec_dev || !data || len <= 0) return ESP_CODEC_DEV_INVALID_ARG;
    return esp_codec_dev_write(g_codec_dev, (void *)data, len);
}



void audio_dac_test_tone(void)
{
    if (!g_codec_dev) return;
    const int N = 160;
    int16_t buf[N * 2];
    float phase = 0.0f;
    const float d = 2.0f * 3.14159265f * 440.0f / 8000.0f;
    ESP_LOGW(TAG, ">>> test tone, 3 s <<<");
    for (int t = 0; t < 150; t++) {
        for (int i = 0; i < N; i++) {
            int16_t s = (int16_t)(sinf(phase) * 8000.0f);
            buf[i * 2] = buf[i * 2 + 1] = s;
            phase += d;
            if (phase >= 6.2831853f) phase -= 6.2831853f;
        }
        esp_codec_dev_write(g_codec_dev, buf, sizeof(buf));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, ">>> tone done <<<");
}