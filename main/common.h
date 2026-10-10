#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "hal/lcd_types.h"
#include "esp_lcd_types.h"
#include "esp_heap_caps.h"  
#include "esp_lcd_touch.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(num, align)  ((num) & ~((align) - 1))



#define EXAMPLE_MIPI_DSI_DPI_CLK_MHZ  40
#define EXAMPLE_MIPI_DSI_LCD_H_RES    800
#define EXAMPLE_MIPI_DSI_LCD_V_RES    1280
#define EXAMPLE_MIPI_DSI_LCD_HSYNC    20
#define EXAMPLE_MIPI_DSI_LCD_HBP      20
#define EXAMPLE_MIPI_DSI_LCD_HFP      40
#define EXAMPLE_MIPI_DSI_LCD_VSYNC    4
#define EXAMPLE_MIPI_DSI_LCD_VBP      12
#define EXAMPLE_MIPI_DSI_LCD_VFP      30


