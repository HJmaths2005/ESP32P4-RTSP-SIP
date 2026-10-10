#pragma once

#include "esp_codec_dev.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "math.h"

/* Global codec handle — accessible from VoIP_manip.c */
extern esp_codec_dev_handle_t g_codec_dev;

/**
 * Initialize I2S + ES8311 codec.
 * Uses the existing i2c_handle from i2c_manip.c (must be called after i2c_init()).
 *
 * @param sample_rate  e.g. 8000 for G.711A
 * @return ESP_OK on success
 */
esp_err_t audio_codec_init(int sample_rate);

/* Convenience wrappers */
int audio_read_mic(void *data, int len);
int audio_write_spk(const void *data, int len);
void audio_dac_test_tone(void);